const express = require('express');
const http = require('http');
const WebSocket = require('ws');
const fetch = require('node-fetch');
const fs = require('fs');
const { stringify } = require('querystring');

const TARGET_URLS = [
  'https://letovocorp.ru/api/auth/login',
  'https://letovocorp.ru/api/auth/isactive/scv',
  'https://letovocorp.ru/api/media/get/images/uploaded/virtual_1.png'
];

const METHODS = ['POST', 'GET', 'GET']

const POLL_INTERVAL = 5000;
const MAX_POINTS = 1000;
const latencyData = [[], [], []];

const app = express();
const server = http.createServer(app);
const wss = new WebSocket.Server({ server });

app.get('/', (req, res) => {
  res.send(`<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <title>Letovo Latency Monitor</title>
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <script src="https://cdn.jsdelivr.net/npm/luxon@3/build/global/luxon.min.js"></script>
  <script src="https://cdn.jsdelivr.net/npm/chartjs-adapter-luxon@1"></script>
  <style>
    body { background-color: #1e1e1e; color: #ccc; }
    canvas { background-color: #2e2e2e; margin-bottom: 30px; }

    /* Панель кнопок */
    #panel {
      margin-bottom: 20px;
    }
    .button {
      display: inline-block;
      margin-right: 10px;
      padding: 10px 20px;
      background-color: #333;
      color: rgb(255, 255, 255);
      text-decoration: none;
      border-radius: 5px;
      transition: background-color 0.2s;
    }
    .button:hover {
      background-color: #555;
    }
  </style>
</head>
<body>
  <h2>Letovo Latency Monitor</h2>
  <div id="panel">
    <a href="/statistics" class="button">Statistics</a>
    <a href="http://10.8.0.1:8010/monitorix" class="button" target="_blank">Monitorix</a>
  </div>
  <canvas id="latencyChart" width="1000" height="500"></canvas>
  <script>
    const TARGET_NAMES = ['simple get', 'auth', 'get file'];
    const chart = new Chart(document.getElementById('latencyChart').getContext('2d'), {
      type: 'line',
      data: {
        datasets: TARGET_NAMES.map(name => ({
          label: name,
          data: [],
          borderWidth: 2,
          fill: false,
          borderColor: 'rgba(0,255,0,1)',
        }))
      },
      options: {
        animation: false,
        plugins: {
          legend: { labels: { color: '#ccc' }, display: true, position: 'top' },
          tooltip: {
            callbacks: {
              label: function(context) {
                const point = context.raw;
                return point ? ('Latency: ' + point.y.toFixed(1) + ' ms | ' + point.customData) : '';
              }
            }
          }
        },
        scales: {
          x: {
            type: 'time',
            time: { unit: 'minute', displayFormats: { minute: 'HH:mm:ss' } },
            ticks: { color: '#ccc' },
            grid: { color: '#444' }
          },
          y: {
            beginAtZero: true,
            title: { display: true, text: 'Latency (ms)', color: '#ccc' },
            ticks: { color: '#ccc' },
            grid: { color: '#444' }
          }
        }
      }
    });

    const socket = new WebSocket('wss://' + location.host + '/latency/letovo/');
    socket.onmessage = (event) => {
      const msg = JSON.parse(event.data);
      const idx = msg.idx;
      const points = msg.data;

      chart.data.datasets[idx].data = points.map(p => ({
        x: new Date(p.time),
        y: p.latency,
        customData: p.reqInfo
      }));

      if (points.length > 0) {
        const latestTime = new Date(points[points.length - 1].time).getTime();
        const RANGE = 45 * 60 * 1000;
        chart.options.scales.x.min = latestTime - RANGE;
        chart.options.scales.x.max = latestTime;
      }

      chart.data.datasets[idx].borderColor = msg.color;
      chart.update();
    };
  </script>
</body>
</html>`);
});

wss.on('connection', ws => {
  console.log('Client connected');
  // при подключении отправляем последние данные для каждого графика
  latencyData.forEach((dataArr, idx) => {
    const lastStatus = dataArr.length > 0 ? dataArr[dataArr.length - 1].status : 200;
    ws.send(JSON.stringify({ idx, data: dataArr, color: statusToColor(lastStatus) }));
  });
});

function measureLatency() {
  TARGET_URLS.forEach((url, idx) => {
    const startTime = Date.now();
    let options = { method: METHODS[idx] }
    if (METHODS[idx] === 'POST') {
      options.body = JSON.stringify({ login: '', password: '' }); // пример тела запроса
    }
    options.headers = {Cookie: 'AuthCookie=scv'}
    fetch(url, options)
      .then(res => res.text().then(body => ({ res, body })))
      .then(({ res }) => {
        const latency = Date.now() - startTime;
        const point = {
          time: new Date(),
          latency,
          reqInfo: `GET ${url} → ${res.status}`,
          status: res.status
        };
        latencyData[idx].push(point);
        if (latencyData[idx].length > MAX_POINTS) latencyData[idx].shift();

        const color = statusToColor(res.status, idx);
        console.log(color);
        const dataToSend = JSON.stringify({
          idx,
          data: latencyData[idx],
          color
        });
        wss.clients.forEach(client => {
          if (client.readyState === WebSocket.OPEN) {
            client.send(dataToSend);
          }
        });
        console.log(`[${point.time.toISOString()}] ${url} latency: ${latency} ms, status: ${res.status}`);
      })
      .catch(err => {
        const latency = Date.now() - startTime;
        const point = {
          time: new Date(),
          latency,
          reqInfo: `GET ${url} → ERROR: ${err.message}`,
          status: 0
        };
        latencyData[idx].push(point);
        if (latencyData[idx].length > MAX_POINTS) latencyData[idx].shift();

        const color = statusToColor(0);
        const dataToSend = JSON.stringify({
          idx,
          data: latencyData[idx],
          color
        });
        wss.clients.forEach(client => {
          if (client.readyState === WebSocket.OPEN) {
            client.send(dataToSend);
          }
        });
        console.error(`[${point.time.toISOString()}] ${url} fetch error: ${err.message}`);
      });
  });
}

function statusToColor(status, idx = 0, decresor=75) {
  if (status >= 200 && status < 300) return `rgba(0, ${255 - idx * decresor}, 0, 1)`;
  if (status >= 400 && status < 500) return `rgba(${255 - idx * decresor}, 200, 0, 1)`;
  if (status >= 500 || status === 0) return `rgba(${255 - idx * decresor}, 0, 0, 1)`;
  return 'rgba(255,0,0,1)';
}

setInterval(measureLatency, POLL_INTERVAL);

const PORT = 8080;
server.listen(PORT, () => {
  console.log(`Monitoring server running on http://localhost:${PORT}/`);
});
