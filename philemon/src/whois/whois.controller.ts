import { Controller, Post, Body } from "@nestjs/common";
import { WhoisService } from "./whois.service";

interface WhoisRequest {
  domain: string;
}

@Controller("v1")
export class WhoisController {
  constructor(private readonly whoisService: WhoisService) {}

  @Post("whois")
  async whoisLookup(@Body() body: WhoisRequest): Promise<{ result: string }> {
    const result = await this.whoisService.lookup(body.domain);
    return { result };
  }
}
