import { Injectable } from "@nestjs/common";
import { request } from "undici";
import { Result, SuccessResult, FailureResult, AppError } from "../result";

interface WeatherData {
  city: string;
  temperature: number;
  humidity: number;
  description: string;
  windSpeed: number;
  source: string;
}

interface ForecastData {
  city: string;
  forecast: Array<{
    date: string;
    temperature: number;
    description: string;
  }>;
  source: string;
}

interface OpenWeatherMapResponse {
  name: string;
  main: {
    temp: number;
    humidity: number;
  };
  weather: Array<{
    description: string;
  }>;
  wind: {
    speed: number;
  };
}

interface OpenWeatherMapForecastResponse {
  city: {
    name: string;
  };
  list: Array<{
    dt_txt: string;
    main: {
      temp: number;
    };
    weather: Array<{
      description: string;
    }>;
  }>;
}

interface WeatherApiResponse {
  location: {
    name: string;
  };
  current: {
    temp_c: number;
    humidity: number;
    condition: {
      text: string;
    };
    wind_kph: number;
  };
}

interface WeatherApiForecastResponse {
  location: {
    name: string;
  };
  forecast: {
    forecastday: Array<{
      date: string;
      day: {
        avgtemp_c: number;
        condition: {
          text: string;
        };
      };
    }>;
  };
}

@Injectable()
export class WeatherService {
  async getWeather(city: string): Promise<Result<WeatherData>> {
    const sources = await Promise.all([
      this.fetchOpenWeatherMap(city),
      this.fetchWeatherApi(city),
    ]);

    const successfulResults: WeatherData[] = [];

    for (const result of sources) {
      if (result.ok) {
        successfulResults.push(result.value);
      }
    }

    if (successfulResults.length === 0) {
      return FailureResult(
        new AppError(`Failed to fetch weather data for city: ${city}`),
      );
    }

    return SuccessResult(this.aggregateWeatherData(successfulResults));
  }

  async getForecast(city: string): Promise<Result<ForecastData>> {
    const sources = await Promise.all([
      this.fetchOpenWeatherMapForecast(city),
      this.fetchWeatherApiForecast(city),
    ]);

    const successfulResults: ForecastData[] = [];

    for (const result of sources) {
      if (result.ok) {
        successfulResults.push(result.value);
      }
    }

    if (successfulResults.length === 0) {
      return FailureResult(
        new AppError(`Failed to fetch forecast data for city: ${city}`),
      );
    }

    return SuccessResult(this.aggregateForecastData(successfulResults));
  }

  private async fetchOpenWeatherMap(
    city: string,
  ): Promise<Result<WeatherData>> {
    try {
      const apiKey = process.env.OPENWEATHER_API_KEY;
      if (!apiKey) {
        return FailureResult(
          new AppError("OPENWEATHER_API_KEY not configured"),
        );
      }

      const { statusCode, body } = await request(
        "https://api.openweathermap.org/data/2.5/weather",
        {
          query: {
            q: city,
            appid: apiKey,
            units: "metric",
          },
        },
      );

      if (statusCode !== 200) {
        return FailureResult(new AppError(`OpenWeatherMap HTTP ${statusCode}`));
      }

      const data = (await body.json()) as OpenWeatherMapResponse;

      return SuccessResult({
        city: data.name,
        temperature: data.main.temp,
        humidity: data.main.humidity,
        description: data.weather[0].description,
        windSpeed: data.wind.speed,
        source: "OpenWeatherMap",
      });
    } catch (error) {
      return FailureResult(
        new AppError(
          `OpenWeatherMap fetch failed: ${error instanceof Error ? error.message : String(error)}`,
        ),
      );
    }
  }

  private async fetchWeatherApi(city: string): Promise<Result<WeatherData>> {
    try {
      const apiKey = process.env.WEATHERAPI_KEY;
      if (!apiKey) {
        return FailureResult(new AppError("WEATHERAPI_KEY not configured"));
      }

      const { statusCode, body } = await request(
        "https://api.weatherapi.com/v1/current.json",
        {
          query: {
            key: apiKey,
            q: city,
          },
        },
      );

      if (statusCode !== 200) {
        return FailureResult(new AppError(`WeatherAPI HTTP ${statusCode}`));
      }

      const data = (await body.json()) as WeatherApiResponse;

      return SuccessResult({
        city: data.location.name,
        temperature: data.current.temp_c,
        humidity: data.current.humidity,
        description: data.current.condition.text,
        windSpeed: data.current.wind_kph / 3.6,
        source: "WeatherAPI",
      });
    } catch (error) {
      return FailureResult(
        new AppError(
          `WeatherAPI fetch failed: ${error instanceof Error ? error.message : String(error)}`,
        ),
      );
    }
  }

  private async fetchOpenWeatherMapForecast(
    city: string,
  ): Promise<Result<ForecastData>> {
    try {
      const apiKey = process.env.OPENWEATHER_API_KEY;
      if (!apiKey) {
        return FailureResult(
          new AppError("OPENWEATHER_API_KEY not configured"),
        );
      }

      const { statusCode, body } = await request(
        "https://api.openweathermap.org/data/2.5/forecast",
        {
          query: {
            q: city,
            appid: apiKey,
            units: "metric",
          },
        },
      );

      if (statusCode !== 200) {
        return FailureResult(
          new AppError(`OpenWeatherMap forecast HTTP ${statusCode}`),
        );
      }

      const data = (await body.json()) as OpenWeatherMapForecastResponse;

      const forecast = data.list.slice(0, 5).map((item) => ({
        date: item.dt_txt,
        temperature: item.main.temp,
        description: item.weather[0].description,
      }));

      return SuccessResult({
        city: data.city.name,
        forecast,
        source: "OpenWeatherMap",
      });
    } catch (error) {
      return FailureResult(
        new AppError(
          `OpenWeatherMap forecast fetch failed: ${error instanceof Error ? error.message : String(error)}`,
        ),
      );
    }
  }

  private async fetchWeatherApiForecast(
    city: string,
  ): Promise<Result<ForecastData>> {
    try {
      const apiKey = process.env.WEATHERAPI_KEY;
      if (!apiKey) {
        return FailureResult(new AppError("WEATHERAPI_KEY not configured"));
      }

      const { statusCode, body } = await request(
        "https://api.weatherapi.com/v1/forecast.json",
        {
          query: {
            key: apiKey,
            q: city,
            days: "5",
          },
        },
      );

      if (statusCode !== 200) {
        return FailureResult(
          new AppError(`WeatherAPI forecast HTTP ${statusCode}`),
        );
      }

      const data = (await body.json()) as WeatherApiForecastResponse;

      const forecast = data.forecast.forecastday.map((day) => ({
        date: day.date,
        temperature: day.day.avgtemp_c,
        description: day.day.condition.text,
      }));

      return SuccessResult({
        city: data.location.name,
        forecast,
        source: "WeatherAPI",
      });
    } catch (error) {
      return FailureResult(
        new AppError(
          `WeatherAPI forecast fetch failed: ${error instanceof Error ? error.message : String(error)}`,
        ),
      );
    }
  }

  private aggregateWeatherData(data: WeatherData[]): WeatherData {
    if (data.length === 1) {
      return data[0];
    }

    const avgTemperature =
      data.reduce((sum, item) => sum + item.temperature, 0) / data.length;
    const avgHumidity =
      data.reduce((sum, item) => sum + item.humidity, 0) / data.length;
    const avgWindSpeed =
      data.reduce((sum, item) => sum + item.windSpeed, 0) / data.length;

    const descriptionCounts = new Map<string, number>();
    data.forEach((item) => {
      descriptionCounts.set(
        item.description,
        (descriptionCounts.get(item.description) || 0) + 1,
      );
    });
    const mostCommonDescription = Array.from(
      descriptionCounts.entries(),
    ).reduce((a, b) => (a[1] > b[1] ? a : b))[0];

    return {
      city: data[0].city,
      temperature: Math.round(avgTemperature * 10) / 10,
      humidity: Math.round(avgHumidity),
      description: mostCommonDescription,
      windSpeed: Math.round(avgWindSpeed * 10) / 10,
      source: `Aggregated (${data.map((d) => d.source).join(", ")})`,
    };
  }

  private aggregateForecastData(data: ForecastData[]): ForecastData {
    if (data.length === 1) {
      return data[0];
    }

    const minLength = Math.min(...data.map((d) => d.forecast.length));
    const aggregatedForecast = [];

    for (let i = 0; i < minLength; i++) {
      const temps = data.map((d) => d.forecast[i].temperature);
      const avgTemp = temps.reduce((sum, t) => sum + t, 0) / temps.length;

      const descriptions = data.map((d) => d.forecast[i].description);
      const descriptionCounts = new Map<string, number>();
      descriptions.forEach((desc) => {
        descriptionCounts.set(desc, (descriptionCounts.get(desc) || 0) + 1);
      });
      const mostCommonDesc = Array.from(descriptionCounts.entries()).reduce(
        (a, b) => (a[1] > b[1] ? a : b),
      )[0];

      aggregatedForecast.push({
        date: data[0].forecast[i].date,
        temperature: Math.round(avgTemp * 10) / 10,
        description: mostCommonDesc,
      });
    }

    return {
      city: data[0].city,
      forecast: aggregatedForecast,
      source: `Aggregated (${data.map((d) => d.source).join(", ")})`,
    };
  }
}
