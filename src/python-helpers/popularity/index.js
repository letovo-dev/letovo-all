const Docker = require('dockerode');
const express = require('express');
const cron = require('node-cron');
const cors = require('cors');

const docker = new Docker({ socketPath: '/var/run/docker.sock' });
const app = express();
app.use(cors());
const startTime = new Date();

// Configuration
const CONTAINER_NAME = 'letovo-server';
const ignoreList = ['auth/login', 'media/get/'];
const WINDOW_SIZE = 30; // snapshots to keep

// In-memory storage
let endpointCounts = {};
const history = [];
let allTImeMax = -1, lastVal = 0;
// Fetch logs and update counts
enabled = true;
async function fetchLogs() {
  try {
    const container = docker.getContainer(CONTAINER_NAME);
    const since = Math.floor(Date.now() / 1000) - 60;
    // Get logs as buffer
    const logsBuffer = await container.logs({ stdout: true, stderr: false, since, timestamps: false });
    const logText = logsBuffer.toString('utf8');
    // Parse lines
    logText.split('\n').forEach(line => {
      if (!line.includes(' TRACE:')) return;
      const match = line.match(/called\s+\/(?<endpoint>[\w\/-]+)/);
      if (match && match.groups) {
        const endpoint = match.groups.endpoint;
        if (!ignoreList.includes(endpoint)) {
          endpointCounts[endpoint] = (endpointCounts[endpoint] || 0) + 1;
        }
      }
    });
    // Snapshot
    const total = Object.values(endpointCounts).reduce((a, b) => a + b, 0);
    history.push({ time: new Date(), data: { ...endpointCounts }, total });
    if (history.length > WINDOW_SIZE) history.shift();
  } catch (err) {
    console.error('Error fetching logs', err);
  }
}

// Compute stats
function computeStats() {
    const total = Object.values(endpointCounts).reduce((a, b) => a + b, 0);
    const endpoints = Object.entries(endpointCounts)
      .map(([name, count]) => ({ name, count, percent: ((count / total) * 100).toFixed(1) }))
      .sort((a, b) => b.count - a.count);
  
    // вычисляем максимум за скользящее окно
    const maxWindow = history.reduce((max, snap) => Math.max(max, snap.total), 0);
    if (maxWindow - lastVal > allTImeMax) allTImeMax = maxWindow - lastVal;
    lastVal = maxWindow;
    const stats = { total, allTImeMax, top: endpoints.slice(0, 5), distribution: endpoints };
  
    return stats;
  }

// Schedule every 2 minutes
cron.schedule('*/1 * * * *', fetchLogs);

// Routes
app.get('/counts', (req, res) => res.json(endpointCounts));
app.get('/stats', (req, res) => res.json(computeStats()));
app.get('/history', (req, res) => res.json(history));
app.get('/start-time', (req, res) => res.json({ startTime: startTime.toISOString() }));

app.use(express.static('public'));
const PORT = process.env.PORT || 5000;
app.listen(PORT, () => console.log(`Server listening on port ${PORT}`));