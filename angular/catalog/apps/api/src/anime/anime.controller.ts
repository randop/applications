import { Body, Controller, Get, Param, Post, Query, BadRequestException, NotFoundException } from '@nestjs/common';
import { ApiOperation, ApiTags } from '@nestjs/swagger';
import { AnimeService } from './anime.service.js';
import { ImportService } from '../import/import.service.js';
import { CreateAnimeFromUrlSchema } from '@anime-catalog/shared/anime-schema';
import { z } from 'zod';

@ApiTags('anime')
@Controller('anime')
export class AnimeController {
  constructor(private readonly anime: AnimeService, private readonly importer: ImportService) {}

  @Get()
  @ApiOperation({ summary: 'List anime records' })
  list(@Query('q') q?: string, @Query('limit') limit?: string, @Query('skip') skip?: string) {
    return this.anime.list(q ?? '', Math.min(Math.max(Number(limit ?? 24), 1), 100), Math.max(Number(skip ?? 0), 0));
  }

  @Get(':id')
  async get(@Param('id') id: string) {
    const result = await this.anime.get(id);
    if (!result) throw new NotFoundException('Anime not found');
    return result;
  }

  @Post('import')
  @ApiOperation({ summary: 'Import anime metadata from a website URL using AI' })
  async importFromUrl(@Body() body: unknown) {
    const parsed = CreateAnimeFromUrlSchema.safeParse(body);
    if (!parsed.success) throw new BadRequestException(z.treeifyError(parsed.error));
    return this.importer.import(parsed.data.url);
  }
}
