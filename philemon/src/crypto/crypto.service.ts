import { Injectable } from "@nestjs/common";
import { request } from "undici";
import { Result, SuccessResult, FailureResult, AppError } from "../result";

interface CryptoRate {
  currency: string;
  rates: { source: string; rate: number }[];
  averageRate: number;
}

interface CryptoRatesData {
  base: string;
  exchanges: CryptoRate[];
  sources: string[];
}

interface CryptoConversionData {
  from: string;
  to: string;
  amount: number;
  converted: number;
  rate: number;
  sources: string[];
}

interface CoinGeckoResponse {
  [currencyId: string]: {
    usd: number;
    [key: string]: number;
  };
}

interface CoinpaprikaResponse {
  base_currency_id: string;
  base_currency_name: string;
  quote_currency_id: string;
  quote_currency_name: string;
  amount: number;
  price: number;
}

interface CryptoCompareResponse {
  RAW: {
    [fromSymbol: string]: {
      [toSymbol: string]: {
        PRICE: number;
      };
    };
  };
}

interface CoinCapResponse {
  data: Array<{
    symbol: string;
    priceUsd: string;
  }>;
}

interface SourceCryptoRates {
  source: string;
  rates: Record<string, number>;
}

@Injectable()
export class CryptoService {
  async getRates(base: string): Promise<Result<CryptoRatesData>> {
    const sources = await Promise.all([
      this.fetchCoinGecko(base),
      this.fetchCoinpaprika(base),
      this.fetchCryptoCompare(base),
      this.fetchCoinCap(base),
    ]);

    const successfulResults: SourceCryptoRates[] = [];

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
        new AppError(`Failed to fetch cryptocurrency rates for base: ${base}`),
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
  ): Promise<Result<CryptoConversionData>> {
    const ratesResult = await this.getRates(from);

    if (!ratesResult.ok) {
      return FailureResult(
        new AppError(`Failed to get cryptocurrency rates for conversion`),
      );
    }

    const ratesData = ratesResult.value;
    const currencyRate = ratesData.exchanges.find((r) => r.currency === to);

    if (!currencyRate) {
      return FailureResult(
        new AppError(`Cryptocurrency rate not available for ${from} to ${to}`),
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

  private async fetchCoinGecko(
    base: string,
  ): Promise<Result<SourceCryptoRates & { base: string }>> {
    try {
      // Map common symbols to CoinGecko IDs
      const symbolToId: Record<string, string> = {
        BTC: "bitcoin",
        ETH: "ethereum",
        USDT: "tether",
        BNB: "binancecoin",
        SOL: "solana",
        XRP: "ripple",
        USDC: "usd-coin",
        ADA: "cardano",
        DOGE: "dogecoin",
        TRX: "tron",
        LINK: "chainlink",
        DOT: "polkadot",
        MATIC: "matic-network",
        LTC: "litecoin",
        UNI: "uniswap",
        XLM: "stellar",
        ATOM: "cosmos",
        AVAX: "avalanche-2",
        SHIB: "shiba-inu",
        DAI: "dai",
        AAVE: "aave",
        ALGO: "algorand",
        AXS: "axie-infinity",
        FTM: "fantom",
        MANA: "decentraland",
        SAND: "the-sandbox",
        NEAR: "near",
        VET: "vechain",
        ICP: "internet-computer",
        ETC: "ethereum-classic",
        XMR: "monero",
        EOS: "eos",
        BCH: "bitcoin-cash",
        FIL: "filecoin",
        XTZ: "tezos",
        MKR: "maker",
        CAKE: "pancakeswap-token",
        FLOW: "flow",
      };

      const baseId = symbolToId[base.toUpperCase()] || base.toLowerCase();
      const url = `https://api.coingecko.com/api/v3/simple/price?ids=${baseId}&vs_currencies=usd,btc,eth&include_24hr_change=true`;
      const { statusCode, body } = await request(url);

      if (statusCode !== 200) {
        return FailureResult(new AppError(`CoinGecko HTTP ${statusCode}`));
      }

      const data = (await body.json()) as CoinGeckoResponse;
      const baseData = data[baseId];

      if (!baseData) {
        return FailureResult(new AppError(`CoinGecko: No data for ${base}`));
      }

      // Invert rates (prices are in terms of USD/BTC/ETH, we want 1 base = X currency)
      // Actually CoinGecko returns: usd: 45000 means 1 BTC = 45000 USD
      // We want rates where base is the FROM currency
      const rates: Record<string, number> = {};
      if (baseData.usd) rates["USD"] = baseData.usd;
      if (baseData.btc) rates["BTC"] = baseData.btc;
      if (baseData.eth) rates["ETH"] = baseData.eth;

      return SuccessResult({
        base: base.toUpperCase(),
        rates,
        source: "CoinGecko",
      });
    } catch (error) {
      return FailureResult(
        new AppError(
          `CoinGecko fetch failed: ${error instanceof Error ? error.message : String(error)}`,
        ),
      );
    }
  }

  private async fetchCoinpaprika(
    base: string,
  ): Promise<Result<SourceCryptoRates & { base: string }>> {
    try {
      // For Coinpaprika, we'll get the price in USD
      const url = `https://api.coinpaprika.com/v1/price-converter?base_currency_id=${base.toLowerCase()}-usd&quote_currency_id=usd-usd&amount=1`;
      const { statusCode, body } = await request(url);

      // Coinpaprika might return 404 for invalid currencies, so we'll try a different approach
      // Get ticker data for top currencies
      const tickerUrl = `https://api.coinpaprika.com/v1/tickers`;
      const { statusCode: tickerStatus, body: tickerBody } =
        await request(tickerUrl);

      if (tickerStatus !== 200) {
        return FailureResult(new AppError(`Coinpaprika HTTP ${tickerStatus}`));
      }

      const tickers = (await tickerBody.json()) as Array<{
        symbol: string;
        quotes: {
          USD: {
            price: number;
          };
        };
      }>;

      const baseTicker = tickers.find(
        (t) => t.symbol.toUpperCase() === base.toUpperCase(),
      );

      if (!baseTicker || !baseTicker.quotes?.USD?.price) {
        return FailureResult(new AppError(`Coinpaprika: No data for ${base}`));
      }

      // Get prices relative to other top cryptocurrencies
      const rates: Record<string, number> = {
        USD: baseTicker.quotes.USD.price,
      };

      // Calculate rates against other top cryptos
      const topCryptos = ["BTC", "ETH", "BNB", "SOL", "XRP", "ADA", "DOGE"];
      for (const crypto of topCryptos) {
        if (crypto === base.toUpperCase()) continue;
        const cryptoTicker = tickers.find((t) => t.symbol === crypto);
        if (cryptoTicker?.quotes?.USD?.price) {
          // Rate = base USD price / crypto USD price
          rates[crypto] =
            baseTicker.quotes.USD.price / cryptoTicker.quotes.USD.price;
        }
      }

      return SuccessResult({
        base: base.toUpperCase(),
        rates,
        source: "Coinpaprika",
      });
    } catch (error) {
      return FailureResult(
        new AppError(
          `Coinpaprika fetch failed: ${error instanceof Error ? error.message : String(error)}`,
        ),
      );
    }
  }

  private async fetchCryptoCompare(
    base: string,
  ): Promise<Result<SourceCryptoRates & { base: string }>> {
    try {
      const toSymbols = "USD,BTC,ETH,BNB,SOL,XRP,ADA,DOGE,DOT,MATIC,LTC,AVAX";
      const url = `https://min-api.cryptocompare.com/data/pricemulti?fsyms=${base.toUpperCase()}&tsyms=${toSymbols}`;
      const { statusCode, body } = await request(url);

      if (statusCode !== 200) {
        return FailureResult(new AppError(`CryptoCompare HTTP ${statusCode}`));
      }

      const data = (await body.json()) as {
        [fromSymbol: string]: {
          [toSymbol: string]: number;
        };
      };

      const baseData = data[base.toUpperCase()];

      if (!baseData) {
        return FailureResult(
          new AppError(`CryptoCompare: No data for ${base}`),
        );
      }

      const rates: Record<string, number> = {};
      for (const [currency, price] of Object.entries(baseData)) {
        rates[currency] = price;
      }

      return SuccessResult({
        base: base.toUpperCase(),
        rates,
        source: "CryptoCompare",
      });
    } catch (error) {
      return FailureResult(
        new AppError(
          `CryptoCompare fetch failed: ${error instanceof Error ? error.message : String(error)}`,
        ),
      );
    }
  }

  private async fetchCoinCap(
    base: string,
  ): Promise<Result<SourceCryptoRates & { base: string }>> {
    try {
      // Get all assets
      const url = `https://api.coincap.io/v2/assets?limit=200`;
      const { statusCode, body } = await request(url);

      if (statusCode !== 200) {
        return FailureResult(new AppError(`CoinCap HTTP ${statusCode}`));
      }

      const data = (await body.json()) as CoinCapResponse;

      // Find the base currency
      const baseAsset = data.data.find(
        (asset) => asset.symbol.toUpperCase() === base.toUpperCase(),
      );

      if (!baseAsset) {
        return FailureResult(new AppError(`CoinCap: No data for ${base}`));
      }

      const basePrice = parseFloat(baseAsset.priceUsd);

      // Calculate rates relative to other top cryptocurrencies
      const rates: Record<string, number> = {
        USD: basePrice,
      };

      const topCryptos = [
        "BTC",
        "ETH",
        "BNB",
        "SOL",
        "XRP",
        "USDC",
        "ADA",
        "DOGE",
        "TRX",
        "LINK",
        "DOT",
        "MATIC",
        "LTC",
        "UNI",
        "XLM",
        "ATOM",
        "AVAX",
        "SHIB",
        "DAI",
      ];

      for (const crypto of topCryptos) {
        if (crypto === base.toUpperCase()) continue;
        const cryptoAsset = data.data.find((asset) => asset.symbol === crypto);
        if (cryptoAsset) {
          const cryptoPrice = parseFloat(cryptoAsset.priceUsd);
          rates[crypto] = basePrice / cryptoPrice;
        }
      }

      return SuccessResult({
        base: base.toUpperCase(),
        rates,
        source: "CoinCap",
      });
    } catch (error) {
      return FailureResult(
        new AppError(
          `CoinCap fetch failed: ${error instanceof Error ? error.message : String(error)}`,
        ),
      );
    }
  }

  private aggregateRatesByCurrency(data: SourceCryptoRates[]): {
    rates: CryptoRate[];
    sources: string[];
  } {
    const currencyMap = new Map<string, { source: string; rate: number }[]>();
    const allSources = new Set<string>();

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

    const rates: CryptoRate[] = [];
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

    rates.sort((a, b) => a.currency.localeCompare(b.currency));

    return {
      rates,
      sources: Array.from(allSources).sort(),
    };
  }
}
