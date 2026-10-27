import { Controller, Get, NotFoundException, Param, Res } from '@nestjs/common';
import { ConfigService } from '@nestjs/config';
import type { Response } from 'express';
import { basename, resolve, join } from 'node:path';
import { access } from 'node:fs/promises';

@Controller('images')
export class ImagesController {
  constructor(private readonly config: ConfigService) {}

  @Get(':filename')
  async getImage(@Param('filename') filename: string, @Res() response: Response) {
    if (basename(filename) !== filename || !/^[a-zA-Z0-9_-]+\.(jpg|png|webp|gif|avif)$/.test(filename)) {
      throw new NotFoundException();
    }
    const root = resolve(this.config.get<string>('IMAGE_STORAGE_DIR', './storage'));
    const path = join(root, filename);
    try { await access(path); } catch { throw new NotFoundException('Image not found'); }
    return response.sendFile(path);
  }
}
