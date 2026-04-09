const EC_CODEWORDS: Record<string, number> = {
  "1-L": 7,
  "1-M": 10,
  "1-Q": 13,
  "1-H": 17,
  "2-L": 10,
  "2-M": 16,
  "2-Q": 22,
  "2-H": 28,
  "3-L": 15,
  "3-M": 26,
  "3-Q": 36,
  "3-H": 44,
};

const DATA_CAPACITY: Record<string, number> = {
  "1-L": 19,
  "1-M": 16,
  "1-Q": 13,
  "1-H": 9,
  "2-L": 34,
  "2-M": 28,
  "2-Q": 22,
  "2-H": 16,
  "3-L": 55,
  "3-M": 44,
  "3-Q": 34,
  "3-H": 26,
};

function getMode(data: string): number {
  if (/^\d+$/.test(data)) return 1;
  if (/^[0-9A-Z $%*+\-./:]+$/.test(data)) return 2;
  return 4;
}

function encodeData(data: string, version: number, ecLevel: string): number[] {
  const key = `${version}-${ecLevel}`;
  const capacity = DATA_CAPACITY[key] || 19;
  const bits: number[] = [];

  const mode = getMode(data);

  if (mode === 4) {
    bits.push(0, 1, 0, 0);
    const lenBits = version <= 1 ? 8 : 16;
    for (let i = lenBits - 1; i >= 0; i--) {
      bits.push((data.length >> i) & 1);
    }
    for (let i = 0; i < data.length; i++) {
      const code = data.charCodeAt(i);
      for (let b = 7; b >= 0; b--) {
        bits.push((code >> b) & 1);
      }
    }
  } else if (mode === 2) {
    const alphaTable = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";
    bits.push(0, 0, 1, 0);
    const lenBits = version <= 1 ? 9 : 11;
    for (let i = lenBits - 1; i >= 0; i--) {
      bits.push((data.length >> i) & 1);
    }
    for (let i = 0; i < data.length; i += 2) {
      if (i + 1 < data.length) {
        const val =
          alphaTable.indexOf(data[i]) * 45 + alphaTable.indexOf(data[i + 1]);
        for (let b = 10; b >= 0; b--) {
          bits.push((val >> b) & 1);
        }
      } else {
        const val = alphaTable.indexOf(data[i]);
        for (let b = 5; b >= 0; b--) {
          bits.push((val >> b) & 1);
        }
      }
    }
  } else {
    bits.push(0, 0, 0, 1);
    const lenBits = version <= 1 ? 10 : 12;
    for (let i = lenBits - 1; i >= 0; i--) {
      bits.push((data.length >> i) & 1);
    }
    for (let i = 0; i < data.length; i += 3) {
      const group = data.substring(i, Math.min(i + 3, data.length));
      const val = parseInt(group, 10);
      const numBits = group.length === 3 ? 10 : group.length === 2 ? 7 : 4;
      for (let b = numBits - 1; b >= 0; b--) {
        bits.push((val >> b) & 1);
      }
    }
  }

  bits.push(0, 0, 0, 0);

  while (bits.length % 8 !== 0) {
    bits.push(0);
  }

  const bytes: number[] = [];
  for (let i = 0; i < bits.length; i += 8) {
    let byte = 0;
    for (let b = 0; b < 8; b++) {
      byte = (byte << 1) | (bits[i + b] || 0);
    }
    bytes.push(byte);
  }

  const padBytes = [0xec, 0x11];
  let padIdx = 0;
  while (bytes.length < capacity) {
    bytes.push(padBytes[padIdx % 2]);
    padIdx++;
  }

  return bytes.slice(0, capacity);
}

function gf256Mul(a: number, b: number): number {
  if (a === 0 || b === 0) return 0;
  const logTable = new Uint8Array(256);
  const expTable = new Uint8Array(256);
  let x = 1;
  for (let i = 0; i < 255; i++) {
    expTable[i] = x;
    logTable[x] = i;
    x = x << 1;
    if (x >= 256) x ^= 0x11d;
  }
  expTable[255] = expTable[0];
  return expTable[(logTable[a] + logTable[b]) % 255];
}

function generateECBytes(data: number[], ecCount: number): number[] {
  const logTable = new Uint8Array(256);
  const expTable = new Uint8Array(256);
  let x = 1;
  for (let i = 0; i < 255; i++) {
    expTable[i] = x;
    logTable[x] = i;
    x = x << 1;
    if (x >= 256) x ^= 0x11d;
  }
  expTable[255] = expTable[0];

  const gen: number[] = [1];
  for (let i = 0; i < ecCount; i++) {
    const newGen = new Array(gen.length + 1).fill(0);
    for (let j = 0; j < gen.length; j++) {
      newGen[j] ^= gen[j];
      if (gen[j] !== 0 && expTable[i] !== 0) {
        const product = expTable[(logTable[gen[j]] + i) % 255];
        newGen[j + 1] ^= product;
      }
    }
    gen.length = 0;
    gen.push(...newGen);
  }

  const msg = [...data, ...new Array(ecCount).fill(0)];
  for (let i = 0; i < data.length; i++) {
    const coeff = msg[i];
    if (coeff !== 0) {
      for (let j = 0; j < gen.length; j++) {
        if (gen[j] !== 0) {
          msg[i + j] ^= expTable[(logTable[gen[j]] + logTable[coeff]) % 255];
        }
      }
    }
  }

  return msg.slice(data.length);
}

function createMatrix(version: number): boolean[][] {
  const size = version * 4 + 17;
  const matrix: boolean[][] = Array.from({ length: size }, () =>
    Array(size).fill(false)
  );
  return matrix;
}

function createReserved(version: number): boolean[][] {
  const size = version * 4 + 17;
  return Array.from({ length: size }, () => Array(size).fill(false));
}

function addFinderPattern(
  matrix: boolean[][],
  reserved: boolean[][],
  row: number,
  col: number
) {
  for (let r = -1; r <= 7; r++) {
    for (let c = -1; c <= 7; c++) {
      const rr = row + r;
      const cc = col + c;
      if (rr < 0 || rr >= matrix.length || cc < 0 || cc >= matrix.length)
        continue;
      reserved[rr][cc] = true;
      if (r >= 0 && r <= 6 && c >= 0 && c <= 6) {
        if (
          r === 0 ||
          r === 6 ||
          c === 0 ||
          c === 6 ||
          (r >= 2 && r <= 4 && c >= 2 && c <= 4)
        ) {
          matrix[rr][cc] = true;
        } else {
          matrix[rr][cc] = false;
        }
      } else {
        matrix[rr][cc] = false;
      }
    }
  }
}

function addTimingPatterns(matrix: boolean[][], reserved: boolean[][]) {
  const size = matrix.length;
  for (let i = 8; i < size - 8; i++) {
    if (!reserved[6][i]) {
      matrix[6][i] = i % 2 === 0;
      reserved[6][i] = true;
    }
    if (!reserved[i][6]) {
      matrix[i][6] = i % 2 === 0;
      reserved[i][6] = true;
    }
  }
}

function reserveFormatBits(reserved: boolean[][], size: number) {
  for (let i = 0; i < 8; i++) {
    reserved[8][i] = true;
    reserved[8][size - 1 - i] = true;
    reserved[i][8] = true;
    reserved[size - 1 - i][8] = true;
  }
  reserved[8][8] = true;
  reserved[size - 8][8] = true;
}

function placeData(
  matrix: boolean[][],
  reserved: boolean[][],
  data: number[]
): void {
  const size = matrix.length;
  let bitIdx = 0;
  const totalBits = data.length * 8;

  let col = size - 1;
  let goingUp = true;

  while (col >= 0) {
    if (col === 6) col--;

    const rows = goingUp
      ? Array.from({ length: size }, (_, i) => size - 1 - i)
      : Array.from({ length: size }, (_, i) => i);

    for (const row of rows) {
      for (let c = 0; c < 2; c++) {
        const cc = col - c;
        if (cc < 0) continue;
        if (reserved[row][cc]) continue;
        if (bitIdx < totalBits) {
          const byteIdx = Math.floor(bitIdx / 8);
          const bitInByte = 7 - (bitIdx % 8);
          matrix[row][cc] = ((data[byteIdx] >> bitInByte) & 1) === 1;
          bitIdx++;
        }
      }
    }

    goingUp = !goingUp;
    col -= 2;
  }
}

function applyMask(
  matrix: boolean[][],
  reserved: boolean[][],
  maskNum: number
): boolean[][] {
  const size = matrix.length;
  const result = matrix.map((row) => [...row]);
  for (let r = 0; r < size; r++) {
    for (let c = 0; c < size; c++) {
      if (reserved[r][c]) continue;
      let flip = false;
      switch (maskNum) {
        case 0:
          flip = (r + c) % 2 === 0;
          break;
        case 1:
          flip = r % 2 === 0;
          break;
        case 2:
          flip = c % 3 === 0;
          break;
        case 3:
          flip = (r + c) % 3 === 0;
          break;
        case 4:
          flip = (Math.floor(r / 2) + Math.floor(c / 3)) % 2 === 0;
          break;
        case 5:
          flip = ((r * c) % 2) + ((r * c) % 3) === 0;
          break;
        case 6:
          flip = (((r * c) % 2) + ((r * c) % 3)) % 2 === 0;
          break;
        case 7:
          flip = (((r + c) % 2) + ((r * c) % 3)) % 2 === 0;
          break;
      }
      if (flip) result[r][c] = !result[r][c];
    }
  }
  return result;
}

function addFormatInfo(
  matrix: boolean[][],
  ecLevel: string,
  maskNum: number
): void {
  const ecBits: Record<string, number> = { L: 1, M: 0, Q: 3, H: 2 };
  let formatInfo = (ecBits[ecLevel] << 3) | maskNum;

  let data = formatInfo << 10;
  const gen = 0b10100110111;
  for (let i = 14; i >= 10; i--) {
    if ((data >> i) & 1) {
      data ^= gen << (i - 10);
    }
  }
  formatInfo = ((formatInfo << 10) | data) ^ 0b101010000010010;

  const size = matrix.length;

  for (let i = 0; i < 6; i++) {
    matrix[8][i] = ((formatInfo >> (14 - i)) & 1) === 1;
  }
  matrix[8][7] = ((formatInfo >> 8) & 1) === 1;
  matrix[8][8] = ((formatInfo >> 7) & 1) === 1;
  matrix[7][8] = ((formatInfo >> 6) & 1) === 1;
  for (let i = 0; i < 6; i++) {
    matrix[5 - i][8] = ((formatInfo >> (9 + i)) & 1) === 1;
  }

  for (let i = 0; i < 7; i++) {
    matrix[size - 1 - i][8] = ((formatInfo >> i) & 1) === 1;
  }
  matrix[size - 8][8] = true;
  for (let i = 0; i < 8; i++) {
    matrix[8][size - 8 + i] = ((formatInfo >> (7 + i)) & 1) === 1;
  }
}

export function generateQRMatrix(data: string): boolean[][] {
  if (!data) return [];

  const version = data.length <= 17 ? 1 : data.length <= 32 ? 2 : 3;
  const ecLevel = "L";
  const key = `${version}-${ecLevel}`;
  const ecCount = EC_CODEWORDS[key] || 7;

  const encoded = encodeData(data, version, ecLevel);
  const ecBytes = generateECBytes(encoded, ecCount);
  const allData = [...encoded, ...ecBytes];

  const matrix = createMatrix(version);
  const reserved = createReserved(version);

  addFinderPattern(matrix, reserved, 0, 0);
  addFinderPattern(matrix, reserved, 0, matrix.length - 7);
  addFinderPattern(matrix, reserved, matrix.length - 7, 0);
  addTimingPatterns(matrix, reserved);
  reserveFormatBits(reserved, matrix.length);

  placeData(matrix, reserved, allData);

  const masked = applyMask(matrix, reserved, 0);
  for (let r = 0; r < matrix.length; r++) {
    for (let c = 0; c < matrix.length; c++) {
      if (!reserved[r][c]) matrix[r][c] = masked[r][c];
    }
  }

  addFormatInfo(matrix, ecLevel, 0);

  return matrix;
}

export function qrToSvg(
  data: string,
  size: number = 120,
  fgColor: string = "currentColor"
): string {
  const matrix = generateQRMatrix(data);
  if (matrix.length === 0) return "";

  const moduleCount = matrix.length;
  const quietZone = 2;
  const totalModules = moduleCount + quietZone * 2;
  const moduleSize = size / totalModules;

  let paths = "";
  for (let r = 0; r < moduleCount; r++) {
    for (let c = 0; c < moduleCount; c++) {
      if (matrix[r][c]) {
        const x = (c + quietZone) * moduleSize;
        const y = (r + quietZone) * moduleSize;
        paths += `<rect x="${x.toFixed(2)}" y="${y.toFixed(2)}" width="${moduleSize.toFixed(2)}" height="${moduleSize.toFixed(2)}" fill="${fgColor}"/>`;
      }
    }
  }

  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${size} ${size}" width="${size}" height="${size}">${paths}</svg>`;
}
