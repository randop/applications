import { Injectable } from '@nestjs/common';
import { InjectModel } from '@nestjs/mongoose';
import { Model } from 'mongoose';
import { Task } from './task.schema';
import { TaskStatus } from '@/constants';
import { DATABASE_CONNECTION } from '@/constants';

@Injectable()
export class TaskRepository {
  constructor(
    @InjectModel(Task.name, DATABASE_CONNECTION) private taskModel: Model<Task>,
  ) {}

  async findAll(): Promise<Task[]> {
    return this.taskModel.find().exec();
  }

  async findOne(id: string): Promise<Task | null> {
    return this.taskModel.findById(id).exec();
  }

  async create(taskData: Partial<Task>): Promise<Task> {
    const createdTask = new this.taskModel(taskData);
    return createdTask.save();
  }

  async update(id: string, taskData: Partial<Task>): Promise<Task | null> {
    return this.taskModel.findByIdAndUpdate(id, taskData, { new: true }).exec();
  }

  async delete(id: string): Promise<boolean> {
    const result = await this.taskModel.findByIdAndDelete(id).exec();
    return !!result;
  }

  async findByStatus(status: TaskStatus): Promise<Task[]> {
    return this.taskModel.find({ status }).exec();
  }
}
