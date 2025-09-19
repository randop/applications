import { Injectable } from '@nestjs/common';
import * as mongoose from 'mongoose';
import { DATABASE_CONNECTION, DATABASE_URL } from '@/constants';
import { ConfigService } from '@nestjs/config';

export const databaseProviders = [
  {
    provide: DATABASE_CONNECTION,
    useFactory: (configService: ConfigService): Promise<typeof mongoose> =>
      mongoose.connect(
        configService.get<string>(DATABASE_URL, 'mongodb://localhost/db'),
      ),
    inject: [ConfigService],
  },
];
