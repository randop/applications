import { Injectable } from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';
import { Interface } from '../entities/interface.entity';
import { Hour } from '../entities/hour.entity';
import { Day } from '../entities/day.entity';
import { Month } from '../entities/month.entity';
import {
  StatsResponseDto,
  StatsDataPointDto,
  InterfacesResponseDto,
} from '../dto/stats-response.dto';

@Injectable()
export class VnstatService {
  constructor(
    @InjectRepository(Interface)
    private interfaceRepository: Repository<Interface>,
    @InjectRepository(Hour)
    private hourRepository: Repository<Hour>,
    @InjectRepository(Day)
    private dayRepository: Repository<Day>,
    @InjectRepository(Month)
    private monthRepository: Repository<Month>
  ) {}

  async getInterfaces(): Promise<InterfacesResponseDto[]> {
    const interfaces = await this.interfaceRepository.find();
    return interfaces.map(iface => ({
      id: iface.id,
      name: iface.name,
      alias: iface.alias,
      active: iface.active === 1,
    }));
  }

  async getHourlyStatsByDateRange(
    interfaceId?: number,
    startDate?: string,
    endDate?: string
  ): Promise<StatsResponseDto[]> {
    const query = this.hourRepository
      .createQueryBuilder('hour')
      .where('hour.date >= :startDate', { startDate: new Date(startDate!) })
      .andWhere('hour.date <= :endDate', { endDate: new Date(endDate!) })
      .orderBy('hour.date', 'ASC');

    if (interfaceId) {
      query.andWhere('hour.interface = :interfaceId', { interfaceId });
    }

    const hours = await query.getMany();
    return this.groupByInterface(hours, 'hour');
  }

  async getHourlyStats(interfaceId?: number, limit: number = 24): Promise<StatsResponseDto[]> {
    const query = this.hourRepository
      .createQueryBuilder('hour')
      .orderBy('hour.date', 'DESC')
      .take(limit);

    if (interfaceId) {
      query.where('hour.interface = :interfaceId', { interfaceId });
    }

    const hours = await query.getMany();
    return this.groupByInterface(hours, 'hour');
  }

  async getDailyStats(interfaceId?: number, limit: number = 30): Promise<StatsResponseDto[]> {
    const query = this.dayRepository
      .createQueryBuilder('day')
      .orderBy('day.date', 'DESC')
      .take(limit);

    if (interfaceId) {
      query.where('day.interface = :interfaceId', { interfaceId });
    }

    const days = await query.getMany();
    return this.groupByInterface(days, 'day');
  }

  async getMonthlyStats(interfaceId?: number, limit: number = 12): Promise<StatsResponseDto[]> {
    const query = this.monthRepository
      .createQueryBuilder('month')
      .orderBy('month.date', 'DESC')
      .take(limit);

    if (interfaceId) {
      query.where('month.interface = :interfaceId', { interfaceId });
    }

    const months = await query.getMany();
    return this.groupByInterface(months, 'month');
  }

  private async groupByInterface(
    data: any[],
    type: 'hour' | 'day' | 'month'
  ): Promise<StatsResponseDto[]> {
    const interfaces = await this.interfaceRepository.find();
    const grouped: { [key: number]: StatsDataPointDto[] } = {};

    for (const item of data) {
      if (!grouped[item.interface]) {
        grouped[item.interface] = [];
      }
      grouped[item.interface].push({
        timestamp: item.date.toISOString(),
        rx: item.rx,
        tx: item.tx,
        rxFormatted: this.humanizeBytes(item.rx),
        txFormatted: this.humanizeBytes(item.tx),
      });
    }

    return Object.keys(grouped).map(key => {
      const iface = interfaces.find(i => i.id === parseInt(key));
      return {
        interfaceId: parseInt(key),
        interfaceName: iface?.name || `Interface ${key}`,
        data: grouped[parseInt(key)].reverse(),
      };
    });
  }

  private humanizeBytes(bytes: number): string {
    const units = ['B', 'KB', 'MB', 'GB', 'TB'];
    let size = bytes;
    let unitIndex = 0;

    while (size >= 1024 && unitIndex < units.length - 1) {
      size /= 1024;
      unitIndex++;
    }

    return `${size.toFixed(2)} ${units[unitIndex]}`;
  }
}
