import {
  Controller,
  Get,
  Post,
  Body,
  Patch,
  Param,
  Delete,
  HttpCode,
  HttpStatus,
  Query,
  ParseUUIDPipe,
} from '@nestjs/common';
import { TasksService } from './tasks.service';
import { Task } from './task.schema';
import { TaskStatus } from '@/constants';

@Controller('tasks')
export class TasksController {
  constructor(private readonly taskService: TasksService) {}

  @Get(':id')
  async findOne(@Param('id') id: string): Promise<Task> {
    return this.taskService.findOne(id);
  }

  @Get()
  async findAll(): Promise<Task[]> {
    return this.taskService.findAll();
  }

  @Post()
  async create(
    @Body()
    createTaskDto: {
      title: string;
      description?: string;
      status?: TaskStatus;
    },
  ): Promise<Task> {
    return this.taskService.create(createTaskDto);
  }

  @Patch(':id')
  async update(
    @Param('id') id: string,
    @Body()
    updateTaskDto: {
      title?: string;
      description?: string;
      status?: TaskStatus;
    },
  ): Promise<Task> {
    return this.taskService.update(id, updateTaskDto);
  }

  @Delete(':id')
  @HttpCode(HttpStatus.NO_CONTENT)
  async remove(@Param('id') id: string): Promise<void> {
    return this.taskService.remove(id);
  }

  @Get('status/:status')
  async findByStatus(@Param('status') status: TaskStatus): Promise<Task[]> {
    return this.taskService.findByStatus(status);
  }
}
