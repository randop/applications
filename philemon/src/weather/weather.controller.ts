import { Controller, Get, Query, BadRequestException } from "@nestjs/common";
import { WeatherService } from "./weather.service";
import { Failure } from "../result";

@Controller("v1/weather")
export class WeatherController {
  constructor(private readonly weatherService: WeatherService) {}

  @Get()
  async getWeather(@Query("city") city: string): Promise<{
    city: string;
    temperature: number;
    humidity: number;
    description: string;
    windSpeed: number;
    source: string;
  }> {
    if (!city) {
      throw new BadRequestException("City parameter is required");
    }
    const result = await this.weatherService.getWeather(city);
    if (!result.ok) {
      const failure = result as Failure<Error>;
      throw new BadRequestException(failure.error.message);
    }
    return result.value;
  }

  @Get("forecast")
  async getForecast(@Query("city") city: string): Promise<{
    city: string;
    forecast: Array<{
      date: string;
      temperature: number;
      description: string;
    }>;
    source: string;
  }> {
    if (!city) {
      throw new BadRequestException("City parameter is required");
    }
    const result = await this.weatherService.getForecast(city);
    if (!result.ok) {
      const failure = result as Failure<Error>;
      throw new BadRequestException(failure.error.message);
    }
    return result.value;
  }
}
