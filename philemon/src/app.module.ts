import { Module } from "@nestjs/common";
import { AppController } from "./app.controller";
import { AppService } from "./app.service";
import { WhoisModule } from "./whois/whois.module";
import { WeatherModule } from "./weather/weather.module";
import { ForexModule } from "./forex/forex.module";
import { CryptoModule } from "./crypto/crypto.module";

@Module({
  imports: [WhoisModule, WeatherModule, ForexModule, CryptoModule],
  controllers: [AppController],
  providers: [AppService],
})
export class AppModule {}
