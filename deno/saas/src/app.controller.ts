import { Controller, Get } from '@nestjs/common';

@Controller()
export class AppController {
  constructor() {}

  @Get()
  getAppName(): string {
    return "SaaS";
  }
}
