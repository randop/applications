import { Module } from '@nestjs/common';
import { TypeOrmModule } from '@nestjs/typeorm';
import { VnstatController } from '../controllers/vnstat.controller';
import { VnstatService } from '../services/vnstat.service';
import { Interface } from '../entities/interface.entity';
import { Hour } from '../entities/hour.entity';
import { Day } from '../entities/day.entity';
import { Month } from '../entities/month.entity';

@Module({
  imports: [TypeOrmModule.forFeature([Interface, Hour, Day, Month])],
  controllers: [VnstatController],
  providers: [VnstatService],
  exports: [VnstatService],
})
export class VnstatModule {}
