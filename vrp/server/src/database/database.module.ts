import { Module } from '@nestjs/common';
import { MongooseModule } from '@nestjs/mongoose';
import { ConfigService } from '@nestjs/config';
import { DATABASE_CONNECTION, DATABASE_URL } from '@/constants';

@Module({
  imports: [
    MongooseModule.forRootAsync({
      connectionName: DATABASE_CONNECTION,
      useFactory: async (configService: ConfigService) => ({
        uri: configService.get<string>(DATABASE_URL, 'mongodb://localhost/db'),
      }),
      inject: [ConfigService],
    }),
  ],
  exports: [MongooseModule],
})
export class DatabaseModule {}
