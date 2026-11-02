import { imageInfo } from './image-info.js';
export async function decodeBrowser(data) {
  const { mime } = imageInfo(data);
  const blob = new Blob([data], { type: mime });
  let bitmap;
  try { bitmap = await createImageBitmap(blob, { imageOrientation: 'from-image' }); }
  catch { throw new Error('INVALID_IMAGE'); }
  try {
    const { width, height } = bitmap;
    if (Math.min(width, height) < 64 || Math.max(width, height) > 8192 || width * height > 24000000)
      throw new Error('INVALID_IMAGE');
    const canvas = new OffscreenCanvas(width, height);
    const ctx = canvas.getContext('2d', { willReadFrequently: true });
    ctx.drawImage(bitmap, 0, 0);
    return { data: ctx.getImageData(0, 0, width, height).data.buffer, width, height, stride: width * 4 };
  } finally { bitmap.close(); }
}
