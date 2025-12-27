import { redis, RedisClient } from "bun";

const log = console;

/*** NOTES:
// Using the default client (reads connection info from environment)
// process.env.REDIS_URL is used by default
await redis.set("hello", "world");
const result = await redis.get("hello");

// Creating a custom client
const client = new RedisClient("redis://username:password@localhost:6379");
await client.set("counter", "0");
await client.incr("counter");
***/

interface CacheClient {
  /**
   * Retrieve a value from the cache by key
   * @param key The key to retrieve
   * @returns The value (string | null)
   */
  get(key: string): Promise<string | null>;

  /**
   * Check if item exist on the cache by key
   * @param key The key to check
   * @returns true if the key exists, false otherwise
   */
  has(key: string): Promise<boolean>;

  /**
   * Sets a value in the cache
   * 
   * @param key The key to set
   * @param value The value to store
   * @param ttl Optional TTL in seconds (simple expiration)
   * @returns true if the key was set, false otherwise
   */
  set(key: string, value: string, ttl?: number): Promise<boolean>;

  /**
   * Removes one or more keys from the cache
   * @param key Single key or array of keys
   * @returns Number of keys actually removed
   */
  remove(key: string | string[]): Promise<number>;
}

export class CacheService implements CacheClient {
  private cache: RedisClient;

  constructor(connectionUrl?: string | null) {
    if (connectionUrl && connectionUrl.trim() !== "") {
      this.cache = new RedisClient(connectionUrl.trim());
    } else {
      this.cache = redis;
    }
  }

  async get(key: string): Promise<string | null> {
    try {
      return await this.cache.get(key);
    } catch {
      return null;
    }
  }

  async has(key: string): Promise<boolean> {
    return this.cache.exists(key);
  }

  async set(key: string, value: string, ttl?: number): Promise<boolean> {
    try {
      await this.cache.set(key, value);
      if (typeof ttl === "number" && ttl > 0) {
        await this.cache.expire(key, ttl);
      }
    } catch {
      return false;
    }
    return true;
  }

  async remove(key: string | string[]): Promise<number> {
    try {
      return await this.cache.del(key);
    } catch {
      return 0;
    }
  }
}

export default CacheService;
