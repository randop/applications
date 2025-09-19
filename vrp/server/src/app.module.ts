import { Module } from '@nestjs/common';
import { AppController } from '@/app.controller';
import { AppService } from '@/app.service';
import { TasksController } from '@/tasks/tasks.controller';
import { DatabaseModule } from '@/database/database.module';
import { ConfigModule } from '@nestjs/config';

@Module({
  imports: [ConfigModule.forRoot({ isGlobal: true }), DatabaseModule],
  controllers: [AppController, TasksController],
  providers: [AppService],
})
export class AppModule {}
