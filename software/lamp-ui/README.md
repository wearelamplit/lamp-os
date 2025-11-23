# Lamp OS Embedded UI Application

This application is uploaded to the board as a filesystem artifact in the Github pipeline. It is mean to run onboard all of the lamps and provide easy configuration from an mobile device.

## Prerequisites

- NodeJS v22 (you can also use NVM if installed)
- VS Code
  - Extensions:
    - Vue
    - Vue 3 Language Support
    - Vitest
    - Vite
    - Prettier
    - Eslint

## Installation

```bash
# Install dependencies
npm ci

# Start development server
npm run dev

# Run tests
npm run test:unit

# Run the linter
npm run lint

# Build for production
npm run build
```

## Usage

Start the dev server by running `node dev-server.js` then run `npm run dev` to see the app in action. The Dev server provides a similar API to the actual embedded boards for easy testing of websocket and rest communication

## Contributing

### Project Structure

```text
src/
├── components/
│   ├── ColorPicker.vue          # Component names must be at least two words
│   └── __tests__/
│       └── ColorPicker.test.ts  # Component tests
├── utils/
│   └── colorUtils.ts            # Utility functions
├── App.vue                      # Root component
└── main.ts                      # Application entry point
```

### Running Tests

```bash
# Run all tests
npm run test:unit

# Run tests in watch mode
npm run test:unit -- --watch
```

### Auto formatting

```bash
# Auto format your files for submission
npm run format
```

### Building

```bash
# Build for production
npm run build

# Preview production build
npm run preview
```

### Uploading to your board

You will need to follow the C++ setup guide in the lamp-os directory before starting.

Plug your lamp board into a usb port

In VSCode, from the `lamp-os` project, navigate to `Pioarduino > Quick Access > Miscellaneous > pioarduino core CLI`

This will bring up a window in the correct environment to upload

```bash
cd ../lamp-ui
npm ci
npm run build:upload
```

This process will build a new .spiffs partition and replace it onboard your esp32.

## Features in Detail

### Mobile Responsiveness

- Optimized for touch interactions
- Responsive layout that adapts to screen size
- Touch-friendly sliders and buttons
- Proper viewport handling
- gzipped to a tiny size for embedded use on the ESP32

### Accessibility

- Proper ARIA labels
- Keyboard navigation support
- Focus management
- Screen reader friendly

## Technologies Used

- **Vue 3**: Progressive JavaScript framework
- **TypeScript**: Type-safe JavaScript
- **Vite**: Fast build tool and dev server
- **Vitest**: Unit testing framework
- **Vue Router**: Client-side routing
- **Pinia**: State management
- **ESLint**: Code linting
- **Prettier**: Code formatting

## Browser Support

- Chrome (latest)
- Firefox (latest)
- Safari (latest)
- Edge (latest)
- Mobile browsers (iOS Safari, Chrome Mobile)

## License

MIT License - see LICENSE file for details.
