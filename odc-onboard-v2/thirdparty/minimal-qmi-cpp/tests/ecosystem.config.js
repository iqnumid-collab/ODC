module.exports = {
  apps: [
    {
      name: "qmi-agent",
      script: "../build/ultima-qmi-controller",
      args: process.env.QMI_CONFIG_PATH || "/etc/ultima-qmi.conf",
      interpreter: "none",
      cwd: __dirname,
      autorestart: false,
      max_restarts: 0,
      error_file: "./logs/qmi-agent-err.log",
      out_file: "./logs/qmi-agent-out.log",
      merge_logs: true,
      log_date_format: "YYYY-MM-DD HH:mm:ss.SSS",
      env: {
        G_MESSAGES_DEBUG: "all",
      },
    },
  ],
};
