const path = require('path');
const http = require('http');
const express = require('express');
const session = require('express-session');
const { Server } = require('socket.io');
const mqtt = require('mqtt');
const Database = require('better-sqlite3');
const bcrypt = require('bcryptjs');

const env = process.env;
const DATA_TOPIC = 'serai_mqtt/sensor/data';
const STATUS_TOPIC = 'serai_mqtt/sensor/status';
const FIELDS = [
  'ornek_id', 'saniye_t', 'gonderim_sayaci',
  'mq3', 'mq4', 'mq135',
  'mq3_filtered', 'mq4_filtered', 'mq135_filtered',
  'mq3_delta', 'mq4_delta', 'mq135_delta', 'gas_delta',
  'temperature', 'humidity', 'gas_resistance', 'fan'
];

// ---------- Veritabanı ----------
const db = new Database(path.join(__dirname, 'serai.db'));
db.exec(`
  CREATE TABLE IF NOT EXISTS readings (
    id INTEGER PRIMARY KEY, ts INTEGER NOT NULL,
    ${FIELDS.map(f => f + ' REAL').join(', ')}
  );
  CREATE INDEX IF NOT EXISTS idx_ts ON readings(ts);
  CREATE INDEX IF NOT EXISTS idx_ornek ON readings(ornek_id);
`);
const insert = db.prepare(
  `INSERT INTO readings (ts, ${FIELDS.join(', ')}) VALUES (@ts, ${FIELDS.map(f => '@' + f).join(', ')})`
);

function query({ from, to, ornek_id }) {
  let sql = 'SELECT * FROM readings WHERE 1=1';
  const p = {};
  if (from) { sql += ' AND ts >= @from'; p.from = +from; }
  if (to) { sql += ' AND ts <= @to'; p.to = +to; }
  if (ornek_id) { sql += ' AND ornek_id = @ornek_id'; p.ornek_id = +ornek_id; }
  return db.prepare(sql + ' ORDER BY ts').all(p);
}

// ---------- Web sunucusu ve giriş ----------
const app = express();
const server = http.createServer(app);
const io = new Server(server);
const sessionMw = session({
  secret: env.SESSION_SECRET || 'degistir',
  resave: false,
  saveUninitialized: false,
  cookie: { maxAge: 24 * 60 * 60 * 1000 }
});
app.use(express.urlencoded({ extended: false }));
app.use(sessionMw);
io.engine.use(sessionMw);
io.use((socket, next) =>
  socket.request.session?.user ? next() : next(new Error('yetkisiz')));

const USER = env.WEB_USER || 'admin';
const HASH = bcrypt.hashSync(env.WEB_PASS || 'degistir-bunu', 10);
const auth = (req, res, next) =>
  req.session.user ? next()
    : req.originalUrl.startsWith('/api') ? res.sendStatus(401) : res.redirect('/login');

app.get('/login', (req, res) => res.sendFile(path.join(__dirname, 'views', 'login.html')));
app.post('/login', (req, res) => {
  const { username, password } = req.body;
  if (username === USER && bcrypt.compareSync(password || '', HASH)) {
    req.session.user = username;
    return res.redirect('/');
  }
  res.redirect('/login?hata=1');
});
app.get('/logout', (req, res) => req.session.destroy(() => res.redirect('/login')));
app.get('/', auth, (req, res) => res.sendFile(path.join(__dirname, 'views', 'index.html')));

// ---------- API ve dışa aktarma ----------
app.get('/api/readings', auth, (req, res) => res.json(query(req.query)));

app.get('/api/export.json', auth, (req, res) => {
  res.attachment('serai.json');
  res.json(query(req.query));
});

app.get('/api/export.csv', auth, (req, res) => {
  const sep = req.query.ayrac || ',';   // Türkçe Excel için ?ayrac=;
  const rows = query(req.query);
  const lines = [['zaman', ...FIELDS].join(sep)];
  for (const r of rows) {
    lines.push([new Date(r.ts).toISOString(), ...FIELDS.map(f => r[f] ?? '')].join(sep));
  }
  res.type('text/csv').attachment('serai.csv').send('\uFEFF' + lines.join('\n'));
});

// ---------- MQTT ----------
let status = 'unknown';
let lastSeen = null;
const client = mqtt.connect(env.MQTT_URL || 'mqtt://localhost:1883', {
  username: env.MQTT_USER || undefined,
  password: env.MQTT_PASS || undefined
});
client.on('connect', () => {
  console.log('MQTT broker bağlantısı kuruldu');
  client.subscribe([DATA_TOPIC, STATUS_TOPIC]);
});
client.on('error', e => console.error('MQTT hatası:', e.message));
client.on('message', (topic, buf) => {
  const text = buf.toString();
  if (topic === STATUS_TOPIC) {
    status = text;
    return io.emit('status', { status, lastSeen });
  }
  let d;
  try { d = JSON.parse(text); } catch { return console.warn('Geçersiz JSON alındı. Gelen bozuk veri:', text); }
  const row = { ts: Date.now() };
  for (const f of FIELDS) row[f] = typeof d[f] === 'boolean' ? +d[f] : (d[f] ?? null);
  insert.run(row);
  lastSeen = row.ts;
  io.emit('reading', row);
});
io.on('connection', s => s.emit('status', { status, lastSeen }));

const PORT = +env.PORT || 3000;
server.listen(PORT, () => console.log(`Panel: http://localhost:${PORT}`));