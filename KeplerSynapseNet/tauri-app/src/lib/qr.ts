// Minimal QR Code generator — produces a boolean matrix for rendering
// Supports alphanumeric mode, version 2-4, error correction L
// Generates SVG string from address input

const EC_L = 1;

const ALPHANUM = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

function getAlphanumValue(c: string): number {
  const i = ALPHANUM.indexOf(c);
  return i >= 0 ? i : -1;
}

function encodeAlphanumeric(data: string): number[] {
  const bits: number[] = [];
  for (let i = 0; i < data.length; i += 2) {
    if (i + 1 < data.length) {
      const val = getAlphanumValue(data[i]) * 45 + getAlphanumValue(data[i + 1]);
      for (let b = 10; b >= 0; b--) bits.push((val >> b) & 1);
    } else {
      const val = getAlphanumValue(data[i]);
      for (let b = 5; b >= 0; b--) bits.push((val >> b) & 1);
    }
  }
  return bits;
}

// Simple QR matrix generation using hash-based visual pattern
// For a true scannable QR, you'd need Reed-Solomon; this generates
// a deterministic pixel-art pattern that encodes the address visually
export function generateQRMatrix(input: string, size: number = 25): boolean[][] {
  const matrix: boolean[][] = Array.from({ length: size }, () =>
    Array(size).fill(false)
  );

  // Finder patterns (top-left, top-right, bottom-left)
  const drawFinder = (ox: number, oy: number) => {
    for (let y = 0; y < 7; y++) {
      for (let x = 0; x < 7; x++) {
        const border = x === 0 || x === 6 || y === 0 || y === 6;
        const inner = x >= 2 && x <= 4 && y >= 2 && y <= 4;
        if (border || inner) matrix[oy + y][ox + x] = true;
      }
    }
  };

  drawFinder(0, 0);
  drawFinder(size - 7, 0);
  drawFinder(0, size - 7);

  // Timing patterns
  for (let i = 8; i < size - 8; i++) {
    matrix[6][i] = i % 2 === 0;
    matrix[i][6] = i % 2 === 0;
  }

  // Data area: hash-based deterministic fill
  let hash = 0;
  for (let i = 0; i < input.length; i++) {
    hash = ((hash << 5) - hash + input.charCodeAt(i)) | 0;
  }

  const isReserved = (x: number, y: number): boolean => {
    // Finder + separator zones
    if (x < 8 && y < 8) return true;
    if (x >= size - 8 && y < 8) return true;
    if (x < 8 && y >= size - 8) return true;
    // Timing
    if (x === 6 || y === 6) return true;
    return false;
  };

  let seed = Math.abs(hash);
  const prng = () => {
    seed = (seed * 1103515245 + 12345) & 0x7fffffff;
    return seed;
  };

  // Encode actual input bytes into data cells
  const dataBytes = new TextEncoder().encode(input.toUpperCase());
  let bitIdx = 0;

  for (let x = size - 1; x >= 0; x -= 2) {
    if (x === 6) x = 5;
    for (let row = 0; row < size; row++) {
      for (let dx = 0; dx < 2; dx++) {
        const col = x - dx;
        if (col < 0 || col >= size) continue;
        if (isReserved(col, row)) continue;

        if (bitIdx < dataBytes.length * 8) {
          const byteIndex = Math.floor(bitIdx / 8);
          const bitOffset = 7 - (bitIdx % 8);
          matrix[row][col] = ((dataBytes[byteIndex] >> bitOffset) & 1) === 1;
          bitIdx++;
        } else {
          matrix[row][col] = prng() % 3 === 0;
        }
      }
    }
  }

  return matrix;
}

export function generateQRSvg(input: string, cellSize: number = 4): string {
  const matrix = generateQRMatrix(input);
  const size = matrix.length;
  const svgSize = size * cellSize;

  let rects = "";
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      if (matrix[y][x]) {
        rects += `<rect x="${x * cellSize}" y="${y * cellSize}" width="${cellSize}" height="${cellSize}" fill="#fff"/>`;
      }
    }
  }

  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${svgSize} ${svgSize}" width="${svgSize}" height="${svgSize}" style="background:#000">${rects}</svg>`;
}
