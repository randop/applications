import { Controller, Get, Param, ParseUUIDPipe } from '@nestjs/common';
import { AppService } from '@/app.service';

@Controller('tasks')
export class TasksController {
  constructor(private readonly appService: AppService) {}

  @Get(':id')
  getTaskById(@Param('id', new ParseUUIDPipe()) id: string): string {
    return `Task: ${id}`;
  }
}
