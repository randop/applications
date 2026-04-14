# Snake Game - WebGL

A basic snake game implemented using WebGL and the lightgl.js library with ES5 syntax.

## Features

- Classic snake gameplay
- 3D graphics using WebGL
- Score tracking
- Game over detection
- Restart functionality
- Fullscreen display

## How to Run

1. Make sure you have the `lightgl.js` file in the same directory as `index.html`
2. Open `index.html` in a web browser
3. The game will start automatically in fullscreen mode

## Controls

- **Arrow Keys**: Move the snake
- **SPACE**: Restart the game (when game over)

## Game Rules

- Control the green snake using arrow keys
- Eat the red food cubes to grow and increase your score
- Avoid hitting the walls or your own tail
- Each food eaten gives you 10 points
- The game ends when you collide with walls or yourself

## Technical Details

- Built with ES5 JavaScript (no modern features)
- Uses lightgl.js for WebGL rendering
- 3D graphics with cubes for snake segments and food
- Grid-based movement system
- Collision detection for walls and self
- Real-time score display

## Files

- `index.html` - Main game file
- `lightgl.js` - WebGL library (already provided)
- `README.md` - This documentation

## Browser Compatibility

This game requires a modern web browser with WebGL support. Tested on:
- Chrome
- Firefox
- Safari
- Edge 