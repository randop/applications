import { Module } from "@nestjs/common";
import { WhoisService } from "./whois.service";
import { WhoisController } from "./whois.controller";

@Module({
  providers: [WhoisService],
  controllers: [WhoisController],
})
export class WhoisModule {}
