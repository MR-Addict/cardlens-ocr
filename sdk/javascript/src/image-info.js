// Inspect encoded dimensions before allocating a decoded bitmap.
export function imageInfo(input) {
  const bytes = new Uint8Array(input.buffer || input, input.byteOffset || 0, input.byteLength);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let width, height, mime;
  if (bytes[0] === 137 && bytes[1] === 80 && bytes[2] === 78 && bytes[3] === 71) {
    mime = 'image/png';
    if (bytes.length < 33 || view.getUint32(4) !== 0x0d0a1a0a || view.getUint32(8) !== 13 || view.getUint32(12) !== 0x49484452)
      throw new Error('INVALID_IMAGE');
    width = view.getUint32(16); height = view.getUint32(20);
  } else if (bytes[0] === 255 && bytes[1] === 216) {
    mime = 'image/jpeg';
    let offset = 2;
    while (offset + 3 < bytes.length) {
      if (bytes[offset++] !== 255) throw new Error('INVALID_IMAGE');
      while (bytes[offset] === 255) ++offset;
      const marker = bytes[offset++];
      if (marker === 0xda || marker === 0xd9) break;
      if (marker === 0x01 || marker >= 0xd0 && marker <= 0xd7) continue;
      if (offset + 2 > bytes.length) break;
      const length = view.getUint16(offset);
      if (length < 2 || offset + length > bytes.length) throw new Error('INVALID_IMAGE');
      if ([0xc0, 0xc1, 0xc2, 0xc3, 0xc5, 0xc6, 0xc7, 0xc9, 0xca, 0xcb, 0xcd, 0xce, 0xcf].includes(marker)) {
        if (length < 8) throw new Error('INVALID_IMAGE');
        height = view.getUint16(offset + 3); width = view.getUint16(offset + 5); break;
      }
      offset += length;
    }
  } else throw new Error('UNSUPPORTED_IMAGE_FORMAT');
  if (!width || !height || Math.min(width, height) < 64 || Math.max(width, height) > 8192 || width * height > 24000000)
    throw new Error('INVALID_IMAGE');
  return { width, height, mime };
}
