import { Module } from "@nestjs/common";
import { ForexService } from "./forex.service";
import { ForexController } from "./forex.controller";

@Module({
  providers: [ForexService],
  controllers: [ForexController],
})
export class ForexModule {}
