import { Injectable } from '@nestjs/common';

@Injectable()
export class AppService {
  getAppName(): string {
    return 'Vibe Resource Planning';
  }
}
