import { Controller, Get, Query, BadRequestException } from "@nestjs/common";
import { ForexService } from "./forex.service";
import { Failure } from "../result";

@Controller("v1/forex")
export class ForexController {
  constructor(private readonly forexService: ForexService) {}

  @Get("rates")
  async getRates(@Query("base") base: string): Promise<{
    base: string;
    exchanges: {
      currency: string;
      rates: { source: string; rate: number }[];
      averageRate: number;
    }[];
    sources: string[];
  }> {
    if (!base) {
      throw new BadRequestException("Base currency parameter is required");
    }
    const result = await this.forexService.getRates(base.toUpperCase());
    if (!result.ok) {
      const failure = result as Failure<Error>;
      throw new BadRequestException(failure.error.message);
    }
    return result.value;
  }

  @Get("convert")
  async convert(
    @Query("from") from: string,
    @Query("to") to: string,
    @Query("amount") amount: string,
  ): Promise<{
    from: string;
    to: string;
    amount: number;
    converted: number;
    rate: number;
    sources: string[];
  }> {
    if (!from || !to || !amount) {
      throw new BadRequestException(
        "from, to, and amount parameters are required",
      );
    }

    const amountNum = parseFloat(amount);
    if (isNaN(amountNum)) {
      throw new BadRequestException("Amount must be a valid number");
    }

    const result = await this.forexService.convert(
      from.toUpperCase(),
      to.toUpperCase(),
      amountNum,
    );

    if (!result.ok) {
      const failure = result as Failure<Error>;
      throw new BadRequestException(failure.error.message);
    }
    return result.value;
  }
}
