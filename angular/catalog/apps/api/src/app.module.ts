import { Module } from '@nestjs/common';
import { ConfigModule } from '@nestjs/config';
import { AnimeController } from './anime/anime.controller.js';
import { AnimeService } from './anime/anime.service.js';
import { MongoService } from './database/mongo.service.js';
import { ImportService } from './import/import.service.js';
import { AiExtractionService } from './import/ai-extraction.service.js';
import { ImageStorageService } from './images/image-storage.service.js';
import { ImagesController } from './images/images.controller.js';

@Module({
  imports: [ConfigModule.forRoot({ isGlobal: true })],
  controllers: [AnimeController, ImagesController],
  providers: [MongoService, AnimeService, ImportService, AiExtractionService, ImageStorageService],
})
export class AppModule {}
