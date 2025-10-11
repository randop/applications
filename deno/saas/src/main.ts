import { NestFactory } from '@nestjs/core';
import { AppModule } from '@/app.module';
import process from "node:process";

const { PORT } = process.env;

async function bootstrap() {
  const app = await NestFactory.create(AppModule);
  await app.listen(PORT ?? 8080);
}
bootstrap();
