// PM2 Ecosystem — HexaRtspCompressor (development)
//
// Manage with the wrapper script (from dev/ directory):
//   ./hexa-pm2 start ecosystem.config.js
//   ./hexa-pm2 logs hexa-bridge --lines 50
//   ./hexa-pm2 list
//   ./hexa-pm2 monit
//   ./hexa-pm2 stop all

const path = require("path");
const ROOT = path.resolve(__dirname, "..");

module.exports = {
  apps: [
    // ── 1. RTSP Source (MediaMTX + looping FFmpeg) ──────────────
    {
      name: "hexa-source",
      script: "./mediamtx",
      args: "mediamtx.yml",
      cwd: `${__dirname}/src-pipeline`,
      interpreter: "none",
      autorestart: true,
      max_restarts: 10,
      restart_delay: 2000,
      watch: false,
      env: {
        MTX_RTSPTRANSPORTS: "tcp",
      },
    },

    // ── 2. RTSP Bridge (transcode + re-serve with auth) ─
    {
      name: "hexa-bridge",
      script: `${ROOT}/build/HexaRtspCompressor`,
      args: `bridge ${__dirname}/config/bridge_local_test.json`,
      cwd: ROOT,
      interpreter: "none",
      autorestart: true,
      max_restarts: 10,
      restart_delay: 4000,
      watch: false,
      kill_timeout: 5000,
    },
  ],
};
