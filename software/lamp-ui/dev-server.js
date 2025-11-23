#!/usr/bin/env node

import express from 'express'
import { WebSocketServer } from 'ws'
import { createServer } from 'http'

const app = express()
const server = createServer(app)
const wss = new WebSocketServer({ server })

const PORT = process.env.DEV_SERVER_PORT || 3001

let settings = {
  lamp: {
    name: 'snafu',
    brightness: 60,
    homeMode: true,
    homeModeSSID: "home-network-01",
    homeModeBrightness: 80,
    password: "my password"
  },
  shade: {
    px: 38,
    colors: ['#4B00634B'],
  },
  base: {
    px: 36,
    colors: ['#9B2E09C5', '#C3520A3D', '#6717005E'],
    ac: 1,
    knockout: [
      { p: 35, b: 14 },
      { p: 17, b: 90 },
      { p: 16, b: 70 },
      { p: 15, b: 50 },
      { p: 14, b: 70 },
      { p: 15, b: 90 },
      {},
      {},
    ],
  },
}

// Middleware
app.use(express.json({ limit: '10mb' }))
app.use(express.urlencoded({ extended: true, limit: '10mb' }))

// CORS middleware for development
app.use((req, res, next) => {
  res.header('Access-Control-Allow-Origin', '*')
  res.header('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE, OPTIONS')
  res.header(
    'Access-Control-Allow-Headers',
    'Origin, X-Requested-With, Content-Type, Accept, Authorization',
  )

  if (req.method === 'OPTIONS') {
    res.sendStatus(200)
  } else {
    next()
  }
})

// Logging utility
const log = (type, message, data = null) => {
  const timestamp = new Date().toISOString()
  const logEntry = {
    timestamp,
    type,
    message,
    data: data ? JSON.stringify(data, null, 2) : null,
  }

  console.log(`[${timestamp}] ${type.toUpperCase()}: ${message}`)
  if (data) {
    console.log('Data:', JSON.stringify(data, null, 2))
  }

  // Broadcast to all connected WebSocket clients
  wss.clients.forEach((client) => {
    if (client.readyState === client.OPEN) {
      client.send(JSON.stringify(logEntry))
    }
  })
}

// Health check endpoint
app.get('/health', (req, res) => {
  res.json({
    status: 'ok',
    timestamp: new Date().toISOString(),
    connections: wss.clients.size,
  })
})

app.get('/settings', (req, res) => {
  res.json(settings)
})

// Log all incoming requests
app.use((req, res, next) => {
  log('REQUEST', `${req.method} ${req.path}`, {
    headers: req.headers,
    query: req.query,
    body: req.body,
  })
  next()
})

// Generic POST endpoint for debugging
app.put('/settings', (req, res) => {
  console.log('Received PUT to /settings', req.body)
  settings = req.body
  res.json({
    success: true,
  })
})

// Generic POST endpoint for debugging
app.post('*', (req, res) => {
  log('POST', `Received POST to ${req.path}`, {
    body: req.body,
    headers: req.headers,
    query: req.query,
  })

  res.json({
    success: true,
    message: 'Data received and logged',
    timestamp: new Date().toISOString(),
    path: req.path,
    receivedData: req.body,
  })
})

// WebSocket connection handling
wss.on('connection', (ws, req) => {
  const clientId = Math.random().toString(36).substr(2, 9)
  log('WEBSOCKET', `Client ${clientId} connected from ${req.socket.remoteAddress}`)

  // Send welcome message
  ws.send(
    JSON.stringify({
      type: 'welcome',
      message: 'Connected to development server',
      clientId,
      timestamp: new Date().toISOString(),
    }),
  )

  ws.on('message', (data) => {
    try {
      const message = JSON.parse(data)
      log('WEBSOCKET_MESSAGE', `Message from client ${clientId}`, message)

      // Echo back the message with server timestamp
      ws.send(
        JSON.stringify({
          type: 'echo',
          originalMessage: message,
          serverTimestamp: new Date().toISOString(),
          clientId,
        }),
      )
    } catch (error) {
      log('WEBSOCKET_ERROR', `Invalid JSON from client ${clientId}`, {
        error: error.message,
        rawData: data.toString(),
      })
    }
  })

  ws.on('close', () => {
    log('WEBSOCKET', `Client ${clientId} disconnected`)
  })

  ws.on('error', (error) => {
    log('WEBSOCKET_ERROR', `Error with client ${clientId}`, { error: error.message })
  })
})

// Error handling
app.use((error, req, res) => {
  log('ERROR', `Server error: ${error.message}`, {
    stack: error.stack,
    path: req.path,
    method: req.method,
  })

  res.status(500).json({
    success: false,
    message: 'Internal server error',
    error: error.message,
    timestamp: new Date().toISOString(),
  })
})

// Start server
server.listen(PORT, () => {
  log('SERVER', `Development server started on port ${PORT}`)
  log('SERVER', `HTTP endpoints available at http://localhost:${PORT}`)
  log('SERVER', `WebSocket available at ws://localhost:${PORT}`)
  log('SERVER', `Health check: http://localhost:${PORT}/health`)
  console.log('\n=== Development Server Ready ===')
  console.log('Send POST requests to any endpoint for debugging')
  console.log('Connect WebSocket clients for real-time logging')
  console.log('Press Ctrl+C to stop\n')
})

// Force shutdown function
const forceShutdown = () => {
  log('SERVER', 'Force shutting down development server...')

  // Close all WebSocket connections immediately
  wss.clients.forEach((client) => {
    if (client.readyState === client.OPEN) {
      client.terminate()
    }
  })

  // Close the server immediately without waiting for connections
  server.close(() => {
    log('SERVER', 'Development server stopped')
    process.exit(0)
  })

  // Force exit after 1 second if server doesn't close
  setTimeout(() => {
    log('SERVER', 'Force exiting...')
    process.exit(1)
  }, 1000)
}

// Graceful shutdown
process.on('SIGINT', forceShutdown)
process.on('SIGTERM', forceShutdown)
