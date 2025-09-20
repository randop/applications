import { Injectable, NotFoundException } from '@nestjs/common';
import { Task } from './task.schema';
import { TaskStatus } from '@/constants';
import { TaskRepository } from './task.repository';

@Injectable()
export class TasksService {
  constructor(private readonly taskRepository: TaskRepository) {}

  async findAll(): Promise<Task[]> {
    return this.taskRepository.findAll();
  }

  async findOne(id: string): Promise<Task> {
    const task = await this.taskRepository.findOne(id);
    if (!task) {
      throw new NotFoundException(`Task with ID ${id} not found`);
    }
    return task;
  }

  async create(createTaskDto: {
    title: string;
    description?: string;
    status?: TaskStatus;
  }): Promise<Task> {
    return this.taskRepository.create(createTaskDto);
  }

  async update(
    id: string,
    updateTaskDto: {
      title?: string;
      description?: string;
      status?: TaskStatus;
    },
  ): Promise<Task> {
    const task = await this.taskRepository.findOne(id);
    if (!task) {
      throw new NotFoundException(`Task with ID ${id} not found`);
    }
    const result = await this.taskRepository.update(id, updateTaskDto);
    return result!;
  }

  async remove(id: string): Promise<void> {
    const success = await this.taskRepository.delete(id);
    if (!success) {
      throw new NotFoundException(`Task with ID ${id} not found`);
    }
  }

  async findByStatus(status: TaskStatus): Promise<Task[]> {
    return this.taskRepository.findByStatus(status);
  }
}
