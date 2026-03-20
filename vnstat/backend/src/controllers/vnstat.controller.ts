import { Controller, Get, Query, ParseIntPipe } from '@nestjs/common';
import { VnstatService } from '../services/vnstat.service';
import { StatsResponseDto, InterfacesResponseDto } from '../dto/stats-response.dto';

@Controller('vnstat')
export class VnstatController {
  constructor(private readonly vnstatService: VnstatService) {}

  @Get('interfaces')
  async getInterfaces(): Promise<InterfacesResponseDto[]> {
    return this.vnstatService.getInterfaces();
  }

  @Get('hourly')
  async getHourlyStats(
    @Query('interfaceId', new ParseIntPipe({ optional: true })) interfaceId?: number,
    @Query('limit', new ParseIntPipe({ optional: true })) limit: number = 24,
    @Query('startDate') startDate?: string,
    @Query('endDate') endDate?: string,
  ): Promise<StatsResponseDto[]> {
    if (startDate && endDate) {
      return this.vnstatService.getHourlyStatsByDateRange(interfaceId, startDate, endDate);
    }
    return this.vnstatService.getHourlyStats(interfaceId, limit);
  }

  @Get('daily')
  async getDailyStats(
    @Query('interfaceId', new ParseIntPipe({ optional: true })) interfaceId?: number,
    @Query('limit', new ParseIntPipe({ optional: true })) limit: number = 30,
  ): Promise<StatsResponseDto[]> {
    return this.vnstatService.getDailyStats(interfaceId, limit);
  }

  @Get('monthly')
  async getMonthlyStats(
    @Query('interfaceId', new ParseIntPipe({ optional: true })) interfaceId?: number,
    @Query('limit', new ParseIntPipe({ optional: true })) limit: number = 12,
  ): Promise<StatsResponseDto[]> {
    return this.vnstatService.getMonthlyStats(interfaceId, limit);
  }
}
