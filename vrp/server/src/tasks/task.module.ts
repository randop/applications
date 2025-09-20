import { Module } from '@nestjs/common';
import { MongooseModule } from '@nestjs/mongoose';
import { TasksController } from './tasks.controller';
import { TasksService } from './tasks.service';
import { Task, TaskSchema } from './task.schema';
import { TaskRepository } from './task.repository';
import { DatabaseModule } from '@/database/database.module';
import { DATABASE_CONNECTION } from '@/constants';
import { AppService } from '@/app.service';

@Module({
  imports: [
    DatabaseModule,
    MongooseModule.forFeature(
      [{ name: Task.name, schema: TaskSchema }],
      DATABASE_CONNECTION,
    ),
  ],
  controllers: [TasksController],
  providers: [TasksService, TaskRepository],
})
export class TaskModule {}
