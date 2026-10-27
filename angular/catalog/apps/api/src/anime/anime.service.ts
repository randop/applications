import { Injectable } from '@nestjs/common';
import { MongoService } from '../database/mongo.service.js';
import type { AnimeRecord } from '@anime-catalog/shared/anime-schema';

@Injectable()
export class AnimeService {
  constructor(private readonly mongo: MongoService) {}

  async list(q: string, limit: number, skip: number) {
    const filter = q.trim()
      ? { $text: { $search: q.trim() } }
      : {};
    const [items, total] = await Promise.all([
      this.mongo.db.collection<AnimeRecord>('anime')
        .find(filter).sort({ createdAt: -1 }).skip(skip).limit(limit).toArray(),
      this.mongo.db.collection('anime').countDocuments(filter),
    ]);
    return { items, total, limit, skip };
  }

  get(id: string) {
    return this.mongo.db.collection<AnimeRecord>('anime').findOne({ id });
  }
}
