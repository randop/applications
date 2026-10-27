import { BadRequestException } from '@nestjs/common';
import { lookup } from 'node:dns/promises';
import ipaddr from 'ipaddr.js';

export async function assertPublicHttpUrl(input: string): Promise<URL> {
  let url: URL;
  try { url = new URL(input); } catch { throw new BadRequestException('Invalid URL'); }
  if (!['http:', 'https:'].includes(url.protocol)) throw new BadRequestException('Only HTTP and HTTPS URLs are allowed');
  if (url.username || url.password) throw new BadRequestException('URLs containing credentials are not allowed');
  if (url.port && !['80', '443'].includes(url.port)) throw new BadRequestException('Only standard HTTP(S) ports are allowed');
  const host = url.hostname.toLowerCase();
  if (host === 'localhost' || host.endsWith('.localhost') || host.endsWith('.local') || host.endsWith('.internal')) {
    throw new BadRequestException('Private hostnames are not allowed');
  }
  const records = await lookup(host, { all: true, verbatim: true }).catch(() => []);
  if (!records.length) throw new BadRequestException('Host could not be resolved');
  for (const record of records) {
    const addr = ipaddr.parse(record.address);
    if (addr.range() !== 'unicast') throw new BadRequestException('Private or reserved IP addresses are not allowed');
  }
  return url;
}

export async function fetchBoundedText(input: string, maxBytes: number): Promise<string> {
  const url = await assertPublicHttpUrl(input);
  const response = await fetch(url, {
    redirect: 'manual',
    headers: { 'User-Agent': 'AnimeCatalogBot/1.0 (+metadata importer)', Accept: 'text/html,application/xhtml+xml,text/plain' },
    signal: AbortSignal.timeout(20000),
  });
  if ([301, 302, 303, 307, 308].includes(response.status)) {
    const location = response.headers.get('location');
    if (!location) throw new BadRequestException('Redirect without a location');
    const redirected = new URL(location, url).toString();
    await assertPublicHttpUrl(redirected);
    return fetchBoundedTextNoRedirect(redirected, maxBytes);
  }
  if (!response.ok) throw new BadRequestException(`Source website returned HTTP ${response.status}`);
  const contentType = response.headers.get('content-type') ?? '';
  if (!/(text\/html|application\/xhtml\+xml|text\/plain)/i.test(contentType)) throw new BadRequestException('Source URL must return an HTML or text page');
  const reader = response.body?.getReader();
  if (!reader) throw new BadRequestException('Source page had no readable body');
  const chunks: Uint8Array[] = [];
  let total = 0;
  while (true) {
    const { done, value } = await reader.read();
    if (done) break;
    total += value.byteLength;
    if (total > maxBytes) { await reader.cancel(); throw new BadRequestException('Source page is too large'); }
    chunks.push(value);
  }
  return new TextDecoder().decode(Buffer.concat(chunks));
}

async function fetchBoundedTextNoRedirect(input: string, maxBytes: number): Promise<string> {
  const url = await assertPublicHttpUrl(input);
  const response = await fetch(url, { redirect: 'error', signal: AbortSignal.timeout(20000), headers: { 'User-Agent': 'AnimeCatalogBot/1.0', Accept: 'text/html,text/plain' } });
  if (!response.ok) throw new BadRequestException(`Redirect target returned HTTP ${response.status}`);
  const contentType = response.headers.get('content-type') ?? '';
  if (!/(text\/html|application\/xhtml\+xml|text\/plain)/i.test(contentType)) throw new BadRequestException('Redirect target is not HTML or text');
  const buffer = Buffer.from(await response.arrayBuffer());
  if (buffer.byteLength > maxBytes) throw new BadRequestException('Source page is too large');
  return buffer.toString('utf8');
}
