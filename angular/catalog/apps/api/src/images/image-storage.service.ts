import { BadRequestException, Injectable } from '@nestjs/common';
import { ConfigService } from '@nestjs/config';
import { mkdir, writeFile } from 'node:fs/promises';
import { extname, join, resolve } from 'node:path';
import { randomUUID } from 'node:crypto';
import { assertPublicHttpUrl } from '../import/url-safety.js';

const allowedTypes: Record<string, string> = {
  'image/jpeg': '.jpg',
  'image/png': '.png',
  'image/webp': '.webp',
  'image/gif': '.gif',
  'image/avif': '.avif',
};

@Injectable()
export class ImageStorageService {
  constructor(private readonly config: ConfigService) {}

  async download(input: string, label: string): Promise<{ publicPath: string }> {
    await assertPublicHttpUrl(input);
    const response = await fetch(input, {
      redirect: 'error',
      signal: AbortSignal.timeout(20000),
      headers: { 'User-Agent': 'AnimeCatalogBot/1.0', Accept: 'image/avif,image/webp,image/png,image/jpeg,image/gif' },
    });
    if (!response.ok) throw new BadRequestException(`Image download failed with HTTP ${response.status}`);
    const type = (response.headers.get('content-type') ?? '').split(';')[0].trim().toLowerCase();
    const extension = allowedTypes[type];
    if (!extension) throw new BadRequestException(`Unsupported image content type: ${type || 'unknown'}`);
    const maxBytes = Number(this.config.get('MAX_IMAGE_BYTES', 12000000));
    const buffer = Buffer.from(await response.arrayBuffer());
    if (buffer.byteLength === 0 || buffer.byteLength > maxBytes) throw new BadRequestException('Image is empty or exceeds the size limit');

    const directory = resolve(this.config.get<string>('IMAGE_STORAGE_DIR', './storage'));
    await mkdir(directory, { recursive: true });
    const safeLabel = label.replace(/[^a-zA-Z0-9_-]/g, '').slice(0, 30) || 'image';
    const filename = `${safeLabel}-${randomUUID()}${extension}`;
    await writeFile(join(directory, filename), buffer, { flag: 'wx' });
    return { publicPath: `/api/images/${filename}` };
  }
}
