import { BadRequestException, ConflictException, Injectable, ServiceUnavailableException } from '@nestjs/common';
import { ConfigService } from '@nestjs/config';
import { v7 as uuidv7 } from 'uuid';
import { XMLParser, XMLValidator } from 'fast-xml-parser';
import { AnimeMetadataSchema, type AnimeMetadata, type AnimeRecord } from '@anime-catalog/shared/anime-schema';
import { MongoService } from '../database/mongo.service.js';
import { AiExtractionService } from './ai-extraction.service.js';
import { ImageStorageService } from '../images/image-storage.service.js';
import { assertPublicHttpUrl, fetchBoundedText } from './url-safety.js';

@Injectable()
export class ImportService {
  constructor(
    private readonly config: ConfigService,
    private readonly mongo: MongoService,
    private readonly ai: AiExtractionService,
    private readonly images: ImageStorageService,
  ) {}

  async import(sourceUrl: string) {
    await assertPublicHttpUrl(sourceUrl);
    const existing = await this.mongo.db.collection<AnimeRecord>('anime').findOne({ sourceUrl });
    if (existing) throw new ConflictException('This source URL has already been imported');

    const page = await fetchBoundedText(sourceUrl, Number(this.config.get('MAX_PAGE_BYTES', 1500000)));
    const xml = await this.ai.extractXml(sourceUrl, page);
    if (XMLValidator.validate(xml) !== true) throw new ServiceUnavailableException('AI returned invalid XML');

    const parser = new XMLParser({ ignoreAttributes: false, attributeNamePrefix: '@_', processEntities: false });
    const parsed = parser.parse(xml)?.anime;
    if (!parsed || typeof parsed !== 'object') throw new BadRequestException('XML must have an <anime> root element');

    const metadataInput = {
      title: parsed.title,
      originalTitle: parsed.originalTitle ?? '',
      alternativeTitle: parsed.alternativeTitle ?? '',
      releaseDate: parsed.releaseDate || undefined,
      genres: parsed.genres?.genre == null ? [] : Array.isArray(parsed.genres.genre) ? parsed.genres.genre : [parsed.genres.genre],
      images: parsed.images?.image == null ? [] : (Array.isArray(parsed.images.image) ? parsed.images.image : [parsed.images.image]).map((img: any) => ({
        id: img['@_id'] || 'image',
        type: img['@_type'] || 'image',
        url: img.url,
        alt: img.alt || '',
      })),
      synopsis: parsed.synopsis ?? '',
    };
    const validation = AnimeMetadataSchema.safeParse(metadataInput);
    if (!validation.success) throw new BadRequestException({ message: 'Extracted metadata failed validation', errors: validation.error.issues });

    const metadata: AnimeMetadata = validation.data;
    const storedImages = [];
    for (const img of metadata.images) {
      const stored = await this.images.download(img.url, img.id);
      storedImages.push({ ...img, localPath: stored.publicPath });
    }

    const now = new Date().toISOString();
    const record: AnimeRecord = {
      ...metadata,
      id: uuidv7(),
      sourceUrl,
      images: storedImages,
      createdAt: now,
      updatedAt: now,
    };
    try {
      await this.mongo.db.collection<AnimeRecord>('anime').insertOne({ ...record, _id: record.id } as any);
    } catch (error: any) {
      if (error?.code === 11000) throw new ConflictException('This source URL has already been imported');
      throw error;
    }
    return record;
  }
}
