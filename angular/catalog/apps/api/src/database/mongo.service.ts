import { Injectable, OnModuleDestroy, OnModuleInit } from '@nestjs/common';
import { ConfigService } from '@nestjs/config';
import { MongoClient, Db } from 'mongodb';

@Injectable()
export class MongoService implements OnModuleInit, OnModuleDestroy {
  private client!: MongoClient;
  db!: Db;

  constructor(private readonly config: ConfigService) {}

  async onModuleInit() {
    this.client = new MongoClient(this.config.get<string>('MONGODB_URI', 'mongodb://localhost:27017'));
    await this.client.connect();
    this.db = this.client.db(this.config.get<string>('MONGODB_DATABASE', 'anime_catalog'));
    await this.db.collection('anime').createIndex({ title: 'text', alternativeTitle: 'text', originalTitle: 'text' });
    await this.db.collection('anime').createIndex({ sourceUrl: 1 }, { unique: true });
  }

  async onModuleDestroy() {
    await this.client?.close();
  }
}
