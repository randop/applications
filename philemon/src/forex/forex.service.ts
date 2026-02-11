import { Injectable } from "@nestjs/common";
import { request } from "undici";
import { Result, SuccessResult, FailureResult, AppError } from "../result";

interface CurrencyRate {
  currency: string;
  rates: { source: string; rate: number }[];
  averageRate: number;
}

interface RatesData {
  base: string;
  exchanges: CurrencyRate[];
  sources: string[];
}

interface ConversionData {
  from: string;
  to: string;
  amount: number;
  converted: number;
  rate: number;
  sources: string[];
}

interface FrankfurterResponse {
  base: string;
  date: string;
  rates: Record<string, number>;
}

interface ExchangeRateApiResponse {
  base_code: string;
  conversion_rates: Record<string, number>;
}

interface CurrencyApiResponse {
  [currencyCode: string]: number;
}

interface ExchangerateHostResponse {
  base: string;
  date: string;
  rates: Record<string, number>;
}

interface VATComplyResponse {
  base: string;
  date: string;
  rates: Record<string, number>;
}

interface SourceRates {
  source: string;
  rates: Record<string, number>;
}

@Injectable()
export class ForexService {
  async getRates(base: string): Promise<Result<RatesData>> {
    const sources = await Promise.all([
      this.fetchFrankfurter(base),
      this.fetchExchangeRateApi(base),
      this.fetchCurrencyApi(base),
      this.fetchExchangerateHost(base),
      this.fetchVATComply(base),
    ]);

    const successfulResults: SourceRates[] = [];

    for (const result of sources) {
      if (
        result.ok &&
        result.value.rates &&
        Object.keys(result.value.rates).length > 0
      ) {
        successfulResults.push({
          source: result.value.source,
          rates: result.value.rates,
        });
      }
    }

    if (successfulResults.length === 0) {
      return FailureResult(
        new AppError(
          `Failed to fetch exchange rates for base currency: ${base}`,
        ),
      );
    }

    const aggregatedData = this.aggregateRatesByCurrency(successfulResults);

    return SuccessResult({
      base: base.toUpperCase(),
      exchanges: aggregatedData.rates,
      sources: aggregatedData.sources,
    });
  }

  async convert(
    from: string,
    to: string,
    amount: number,
  ): Promise<Result<ConversionData>> {
    const ratesResult = await this.getRates(from);

    if (!ratesResult.ok) {
      return FailureResult(
        new AppError(`Failed to get exchange rates for conversion`),
      );
    }

    const ratesData = ratesResult.value;
    const currencyRate = ratesData.exchanges.find((r) => r.currency === to);

    if (!currencyRate) {
      return FailureResult(
        new AppError(`Exchange rate not available for ${from} to ${to}`),
      );
    }

    return SuccessResult({
      from,
      to,
      amount,
      converted: Math.round(amount * currencyRate.averageRate * 100) / 100,
      rate: Math.round(currencyRate.averageRate * 100000) / 100000,
      sources: currencyRate.rates.map((r) => r.source),
    });
  }

  private async fetchFrankfurter(
    base: string,
  ): Promise<Result<SourceRates & { base: string }>> {
    try {
      const { statusCode, body } = await request(
        "https://api.frankfurter.app/latest",
        {
          query: { from: base },
        },
      );

      if (statusCode !== 200) {
        return FailureResult(new AppError(`Frankfurter HTTP ${statusCode}`));
      }

      const data = (await body.json()) as FrankfurterResponse;

      return SuccessResult({
        base: data.base,
        rates: data.rates,
        source: "Frankfurter",
      });
    } catch (error) {
      return FailureResult(
        new AppError(
          `Frankfurter fetch failed: ${error instanceof Error ? error.message : String(error)}`,
        ),
      );
    }
  }

  private async fetchExchangeRateApi(
    base: string,
  ): Promise<Result<SourceRates & { base: string }>> {
    try {
      const apiKey = process.env.EXCHANGERATE_API_KEY;
      if (!apiKey) {
        return FailureResult(
          new AppError("EXCHANGERATE_API_KEY not configured"),
        );
      }

      // Note: base is part of the path, not query parameter
      const { statusCode, body } = await request(
        `https://v6.exchangerate-api.com/v6/${apiKey}/latest/${base}`,
      );

      if (statusCode !== 200) {
        return FailureResult(
          new AppError(`ExchangeRate-API HTTP ${statusCode}`),
        );
      }

      const data = (await body.json()) as ExchangeRateApiResponse;

      return SuccessResult({
        base: data.base_code,
        rates: data.conversion_rates,
        source: "ExchangeRate-API",
      });
    } catch (error) {
      return FailureResult(
        new AppError(
          `ExchangeRate-API fetch failed: ${error instanceof Error ? error.message : String(error)}`,
        ),
      );
    }
  }

  private async fetchCurrencyApi(
    base: string,
  ): Promise<Result<SourceRates & { base: string }>> {
    try {
      // Note: base is part of the path (lowercase), not query parameter
      const { statusCode, body } = await request(
        `https://cdn.jsdelivr.net/npm/@fawazahmed0/currency-api@latest/v1/currencies/${base.toLowerCase()}.json`,
      );

      if (statusCode !== 200) {
        return FailureResult(new AppError(`Currency-api HTTP ${statusCode}`));
      }

      const data = (await body.json()) as {
        date: string;
      } & CurrencyApiResponse;
      const rates = data[base.toLowerCase()];

      if (!rates || typeof rates !== "object") {
        return FailureResult(
          new AppError(
            `Currency-api returned invalid data structure for ${base}`,
          ),
        );
      }

      const normalizedRates: Record<string, number> = {};
      for (const [currency, rate] of Object.entries(rates)) {
        if (typeof rate === "number") {
          normalizedRates[currency.toUpperCase()] = rate;
        }
      }

      return SuccessResult({
        base: base.toUpperCase(),
        rates: normalizedRates,
        source: "Currency-api",
      });
    } catch (error) {
      return FailureResult(
        new AppError(
          `Currency-api fetch failed: ${error instanceof Error ? error.message : String(error)}`,
        ),
      );
    }
  }

  private async fetchExchangerateHost(
    base: string,
  ): Promise<Result<SourceRates & { base: string }>> {
    try {
      const { statusCode, body } = await request(
        "https://api.exchangerate.host/latest",
        {
          query: { base },
        },
      );

      if (statusCode !== 200) {
        return FailureResult(
          new AppError(`Exchangerate.host HTTP ${statusCode}`),
        );
      }

      const data = (await body.json()) as ExchangerateHostResponse;

      return SuccessResult({
        base: data.base,
        rates: data.rates,
        source: "Exchangerate.host",
      });
    } catch (error) {
      return FailureResult(
        new AppError(
          `Exchangerate.host fetch failed: ${error instanceof Error ? error.message : String(error)}`,
        ),
      );
    }
  }

  private async fetchVATComply(
    base: string,
  ): Promise<Result<SourceRates & { base: string }>> {
    try {
      const { statusCode, body } = await request(
        "https://api.vatcomply.com/rates",
        {
          query: { base },
        },
      );

      if (statusCode !== 200) {
        return FailureResult(new AppError(`VATComply HTTP ${statusCode}`));
      }

      const data = (await body.json()) as VATComplyResponse;

      return SuccessResult({
        base: data.base,
        rates: data.rates,
        source: "VATComply",
      });
    } catch (error) {
      return FailureResult(
        new AppError(
          `VATComply fetch failed: ${error instanceof Error ? error.message : String(error)}`,
        ),
      );
    }
  }

  private aggregateRatesByCurrency(data: SourceRates[]): {
    rates: CurrencyRate[];
    sources: string[];
  } {
    const currencyMap = new Map<string, { source: string; rate: number }[]>();
    const allSources = new Set<string>();

    // Group rates by currency across all sources
    data.forEach((sourceData) => {
      allSources.add(sourceData.source);
      Object.entries(sourceData.rates).forEach(([currency, rate]) => {
        if (!currencyMap.has(currency)) {
          currencyMap.set(currency, []);
        }
        currencyMap.get(currency)!.push({
          source: sourceData.source,
          rate,
        });
      });
    });

    // Convert map to array format with averages
    const rates: CurrencyRate[] = [];
    currencyMap.forEach((sourceRates, currency) => {
      const avgRate =
        sourceRates.reduce((sum, r) => sum + r.rate, 0) / sourceRates.length;
      rates.push({
        currency,
        rates: sourceRates.map((r) => ({
          source: r.source,
          rate: Math.round(r.rate * 100000) / 100000,
        })),
        averageRate: Math.round(avgRate * 100000) / 100000,
      });
    });

    // Sort by currency code
    rates.sort((a, b) => a.currency.localeCompare(b.currency));

    return {
      rates,
      sources: Array.from(allSources).sort(),
    };
  }
}
