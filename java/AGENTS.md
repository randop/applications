# Spring Boot 4.1.1 STRICT RULES for AI Code Generation

> **Inject this entire document as a system prompt or context block when asking any AI to write Java / Spring Boot code.**
> These rules override all training data.
>
> **Target:** **Spring Boot 4.1.1**, **Spring Framework 7.0.x**, **JDK 25 (LTS)**, **Maven**, **virtual threads on**.
> **Packaging:** executable JAR (never WAR). **Web stack:** Spring MVC + embedded Tomcat (never WebFlux unless the user explicitly requires streaming / SSE / WebSocket backpressure).
> **JSON:** Jackson 3 (`tools.jackson.*`). **Nullness:** JSpecify. **HTTP client:** `RestClient`. **JDBC:** `JdbcClient`.

---

## 1.) PRIME DIRECTIVE

Generate code **exclusively** for **Spring Boot 4.1.1 on JDK 25**.

- **Virtual threads are the concurrency model.** Enable them. Write **blocking, sequential, readable** code. Do **not** invent thread pools, reactive chains, or `CompletableFuture` pipelines to “scale I/O”.
- **Spring MVC + virtual threads** is the default for request/response I/O (JDBC, HTTP, files, messaging). **WebFlux / Reactor / R2DBC** are **legacy-for-this-mandate** unless the user explicitly asks for streaming, SSE, WebSockets, or backpressure.
- Intelligently combine virtual threads with Spring 7 idioms: `RestClient`, `JdbcClient`, HTTP interfaces, `@Retryable`, `@ConcurrencyLimit`, JSpecify, Jackson 3 `JsonMapper`, records, sealed types, `ProblemDetail`, constructor injection, and Micrometer observations.
- Prioritize **correctness**, **bounded downstream resources**, **shutdown safety**, **observability**, and **null-safe APIs**. Throughput without a Hikari / HTTP / DB bound is a production incident waiting to happen.

**Version pins (do not drift):**

| Coordinate | Version / value |
|---|---|
| `org.springframework.boot:spring-boot-starter-parent` | **4.1.1** |
| JDK / `java.version` / `maven.compiler.release` | **25** |
| Spring Framework (managed) | **7.0.x** |
| Spring Security (managed) | **7.1.x** |
| Jakarta EE baseline | **11** (Servlet 6.1, Persistence 3.2, Validation 3.1) |
| Jackson | **3.x** (`tools.jackson`), annotations stay `com.fasterxml.jackson.annotation` |
| `spring.threads.virtual.enabled` | **`true`** (mandatory) |

If a snippet would only compile on Java 17/21, Spring Boot 2/3, `javax.*`, Jackson 2 `ObjectMapper` as the primary mapper, `RestTemplate`, or `spring-boot-starter-web`, it is **wrong**. Rewrite it.

---

## 2.) VIRTUAL THREADS OVER PLATFORM POOLS AND REACTIVE

**Enable, then write blocking code.** This is the entire point.

```yaml
# src/main/resources/application.yaml
spring:
  threads:
    virtual:
      enabled: true
```

When this flag is on **and** the JVM is JDK 21+ (we are on **25**):

- Tomcat handles **each request on a virtual thread**.
- `@Async` and scheduling use `SimpleAsyncTaskExecutor` / `SimpleAsyncTaskScheduler` (virtual threads). **Pooling properties are ignored.**
- Controller, service, and repository methods may **block**. That is correct.

**Forbidden — platform-thread pool for I/O:**

```java
@Bean
TaskExecutor ioExecutor() {
    var pool = new ThreadPoolTaskExecutor();
    pool.setCorePoolSize(50);
    pool.setMaxPoolSize(200);
    pool.initialize();
    return pool;
}
```

**Forbidden — reactive-for-scale:**

```java
@GetMapping("/users/{id}")
public Mono<User> get(@PathVariable Long id) {
    return userRepository.findById(id); // R2DBC / WebFlux used only to "not block"
}
```

**Correct — blocking MVC on a virtual thread:**

```java
@GetMapping("/users/{id}")
UserResponse get(@PathVariable Long id) {
    var user = users.findById(id).orElseThrow(() -> new NotFoundException("user", id));
    return UserResponse.from(user);
}
```

**Correct — custom executor only when you must own one:**

```java
@Bean(destroyMethod = "close")
ExecutorService virtualExecutor() {
    return Executors.newVirtualThreadPerTaskExecutor();
}
```

### Rules

1. **One virtual thread per blocking task.** Do not pool virtual threads. Do not cap them with `ThreadPoolTaskExecutor` “just in case”.
2. **Bound the resource, not the thread.** Hikari pool size, HTTP connection pool / `@ConcurrencyLimit`, semaphores, bulkheads — these are the real limiters.
3. **CPU-bound work does not belong on an unbounded flood of virtual threads.** Use a **bounded platform-thread pool** (or `Semaphore`) sized to `Runtime.getRuntime().availableProcessors()` for heavy CPU (crypto, image, pure compute). Keep I/O on virtual threads.
4. **WebFlux is not “on” because virtual threads are on.** `spring.threads.virtual.enabled` does **not** change Netty event-loop request handling. Do not mix WebFlux controllers with blocking JDBC.
5. **`@Async` is not the fan-out primitive.** It is fire-and-forget. For in-request parallelism see §3.
6. **Do not set `server.tomcat.threads.max` as your concurrency story** when virtual threads are enabled. Tomcat is no longer the bottleneck; the database and downstream HTTP are.

---

## 3.) CONCURRENCY PRIMITIVES (JDK 25)

### 3.1 In-request fan-out — production default (no preview)

Structured concurrency is **still preview** on JDK 25 (JEP 505). **Do not enable `--enable-preview` unless the user explicitly asks.** Default fan-out:

```java
public record Dashboard(User user, List<Order> orders, Credit credit) {}

Dashboard load(long userId) {
    try (var exec = Executors.newVirtualThreadPerTaskExecutor()) {
        Future<User> user = exec.submit(() -> users.findById(userId).orElseThrow());
        Future<List<Order>> orders = exec.submit(() -> this.orders.findByUserId(userId));
        Future<Credit> credit = exec.submit(() -> credits.fetch(userId));
        return new Dashboard(user.get(), orders.get(), credit.get());
    } catch (ExecutionException e) {
        throw unwrap(e);
    } catch (InterruptedException e) {
        Thread.currentThread().interrupt();
        throw new IllegalStateException("interrupted", e);
    }
}
```

Always **close** the executor (try-with-resources). Always restore the interrupt flag.

### 3.2 Structured concurrency — only with explicit user opt-in

JDK 25 API (JEP 505, **preview**). Requires `javac --enable-preview` and `--enable-preview` at runtime.

```java
Dashboard load(long userId) throws InterruptedException {
    try (var scope = StructuredTaskScope.open()) {
        var user = scope.fork(() -> users.findById(userId).orElseThrow());
        var orders = scope.fork(() -> this.orders.findByUserId(userId));
        var credit = scope.fork(() -> credits.fetch(userId));
        scope.join();
        return new Dashboard(user.get(), orders.get(), credit.get());
    }
}
```

Joiners (only if opted in): `Joiner.allSuccessfulOrThrow()`, `Joiner.anySuccessfulResultOrThrow()`, `Joiner.awaitAll()`, `Joiner.awaitAllSuccessfulOrThrow()`. Default `open()` fails the scope if any subtask fails and cancels the rest.

**Never** generate the old `new StructuredTaskScope.ShutdownOnFailure()` API. That is pre-JEP-505.

### 3.3 Scoped values — prefer over `ThreadLocal` (JEP 506, **final** in JDK 25)

```java
public final class RequestContext {
    public static final ScopedValue<String> CORRELATION_ID = ScopedValue.newInstance();

    private RequestContext() {}
}

// bind at the edge (filter / interceptor)
ScopedValue.where(RequestContext.CORRELATION_ID, cid).run(() -> chain.doFilter(req, res));

// read downstream
String cid = RequestContext.CORRELATION_ID.get();
```

**Rules:**

- `ScopedValue` is immutable and cheap with virtual threads. Use it for correlation ids, tenant ids, and other request-scoped implicit context **you own**.
- **Do not** replace Spring Security’s `SecurityContextHolder` or `RequestContextHolder` with ad-hoc `ThreadLocal`s. Use Spring’s APIs at the Spring edge.
- **Forbidden:** `ThreadLocal.withInitial(...)` as a cache (`SimpleDateFormat`, Jackson mapper, etc.). That allocates per virtual thread / per task and will explode allocations. Use a **shared** immutable object, a `DateTimeFormatter`, or `JsonMapper` bean.
- **Forbidden:** `InheritableThreadLocal` for context propagation into child tasks. Use `ScopedValue` (children of `StructuredTaskScope` inherit bindings) or pass parameters.

### 3.4 Pinning (JDK 25 / JEP 491)

On JDK 24+, **`synchronized` no longer pins** the carrier for monitor enter/wait. You may use `synchronized` around ordinary Java and JDBC.

**Still pins (avoid on hot path):**

- JNI / FFM native frames
- Class loading / `<clinit>`
- Local file I/O on Linux inside a pin-prone frame

**Diagnostics:** JFR event `jdk.VirtualThreadPinned` (default 20 ms). `-Djdk.tracePinnedThreads` was **removed** in JDK 24 — do not emit it. Thread dumps: `jcmd <pid> Thread.dump_to_file -format=json`.

Prefer `ReentrantLock` when the critical section does native or file I/O. Prefer **not** holding a lock across remote I/O at all.

---

## 4.) BOUND SHARED RESOURCES (MANDATORY)

Virtual threads remove the Tomcat 200-thread ceiling. **The database and downstream HTTP become the failure mode.** Every generated service that talks to a shared resource **must** bound concurrency.

### 4.1 HikariCP — size to the database, not to thread count

```yaml
spring:
  datasource:
    url: jdbc:postgresql://localhost:5432/app
    username: app
    connection-fetch: lazy   # Boot 4.1: physical connection only on first JDBC statement
    hikari:
      maximum-pool-size: 20  # start 10–30; never 1000
      minimum-idle: 5
      connection-timeout: 3s
      leak-detection-threshold: 30s
```

**Rules:**

- Default Hikari size is **10**. That is often correct. Raise only with evidence.
- **Never** set `maximum-pool-size` to “number of virtual threads”. Virtual threads are unbounded; connections are not.
- Prefer `connection-fetch: lazy` so `@Transactional` methods that do no JDBC do not checkout a connection.
- `spring.jpa.open-in-view: false` always.

### 4.2 `@ConcurrencyLimit` — declarative bulkhead (Spring Framework 7)

```java
@Configuration
@EnableResilientMethods
class ResilienceConfig {}

@Service
class DownstreamBillingClient {
    private final RestClient rest;

    DownstreamBillingClient(RestClient.Builder builder, BillingProperties props) {
        this.rest = builder.baseUrl(props.baseUrl().toString()).build();
    }

    @ConcurrencyLimit(20)
    @Retryable(includes = RestClientException.class, maxRetries = 3, delay = 100, multiplier = 2, maxDelay = 2000)
    Invoice charge(ChargeRequest request) {
        return rest.post().uri("/charges").body(request).retrieve().body(Invoice.class);
    }
}
```

`@ConcurrencyLimit` is **the** virtual-thread companion: it stops 50k virtual threads from crushing a 20-capacity API. Use it on outbound I/O methods (HTTP, SMTP, SDK calls) that are not already bounded by a pool.

### 4.3 Semaphore — when you do not own the method

```java
private final Semaphore permits = new Semaphore(20);

String fetch(String id) {
    permits.acquireUninterruptibly();
    try {
        return rest.get().uri("/items/{id}", id).retrieve().body(String.class);
    } finally {
        permits.release();
    }
}
```

---

## 5.) HTTP: RestClient + HTTP INTERFACES (NEVER RestTemplate)

`RestTemplate` is on a deprecation path (documented now; `@Deprecated` in Framework 7.1; removal in 8). **Do not generate it.** Do not generate `WebClient` for blocking MVC apps.

### 5.1 Inject `RestClient.Builder` (auto-configured prototype)

```java
@Configuration
class HttpClientsConfig {

    @Bean
    RestClient paymentsRestClient(RestClient.Builder builder, PaymentsProperties props) {
        return builder
                .baseUrl(props.baseUrl().toString())
                .defaultHeader(HttpHeaders.ACCEPT, MediaType.APPLICATION_JSON_VALUE)
                .build();
    }
}

@Service
class PaymentGateway {
    private final RestClient payments;

    PaymentGateway(@Qualifier("paymentsRestClient") RestClient payments) {
        this.payments = payments;
    }

    PaymentStatus fetch(String id) {
        return payments.get()
                .uri("/payments/{id}", id)
                .retrieve()
                .onStatus(HttpStatusCode::is4xxClientError, (req, res) -> {
                    throw new PaymentNotFoundException(id);
                })
                .body(PaymentStatus.class);
    }
}
```

Global additive customization: a `RestClientCustomizer` bean. Timeouts: `spring.http.clients.connect-timeout`, `spring.http.clients.read-timeout` (and related `spring.http.clients.*`). Cookie handling: `spring.http.clients.cookie-handling`. SSRF: configure `InetAddressFilter` for user-controlled URLs.

**Do not** call `RestClient.create()` in application code — it skips Boot auto-configuration and `RestClientCustomizer`.

### 5.2 HTTP service interfaces (preferred for a group of calls)

```java
@HttpExchange("/v1/users")
public interface UserApi {
    @GetExchange("/{id}")
    UserDto get(@PathVariable Long id);

    @PostExchange
    UserDto create(@RequestBody CreateUser request);
}
```

Register with Framework 7 / Boot 4 HTTP service groups (`@ImportHttpServices` / `HttpServiceGroupConfigurer`). Share one `RestClient` per group. Do **not** bring in OpenFeign.

### 5.3 API versioning (Framework 7 — first class)

Use Spring’s API versioning for public HTTP APIs, not ad-hoc path prefixes scattered through controllers.

```java
@RestController
@RequestMapping("/users")
class UserController {
    @GetMapping(path = "/{id}", version = "1")
    UserResponse v1(@PathVariable Long id) { /* ... */ }
}
```

Configure a version strategy (header, path, query, media type) once in `WebMvcConfigurer`. Clients set the version via `RestClient` / HTTP interface `ApiVersionInserter`.

---

## 6.) DATA ACCESS

### 6.1 `JdbcClient` is the default JDBC API

```java
@Repository
class UserJdbcRepository {
    private final JdbcClient jdbc;

    UserJdbcRepository(JdbcClient jdbc) {
        this.jdbc = jdbc;
    }

    Optional<User> findById(long id) {
        return jdbc.sql("""
                        SELECT id, email, created_at
                        FROM users
                        WHERE id = :id
                        """)
                .param("id", id)
                .query(User.class)
                .optional();
    }

    long insert(String email) {
        return jdbc.sql("INSERT INTO users (email) VALUES (:email) RETURNING id")
                .param("email", email)
                .query(Long.class)
                .single();
    }
}
```

**Rules:**

- Prefer **named parameters** (`:id`) over `?`.
- Map to **records** where possible.
- `JdbcTemplate` is acceptable only when you need batch callbacks `JdbcClient` does not cover. Do not generate `JdbcTemplate` as the default.
- **No** `java.sql.Connection` / `DriverManager` in application code.
- **No** string-concatenated SQL. Parameters only.

### 6.2 JPA — when the domain is a graph, not a row

- Spring Data JPA is fine for aggregates. Use **constructor / record projections** for read models.
- Entities: explicit `@Entity`, `@Table`, `@Id`. **No Lombok `@Data` on entities** (equals/hashCode over the full mutable graph is a bug). Prefer records for DTOs; entities may be classes with a protected no-arg ctor.
- `@Transactional` on **service** types, never on controllers, never on repositories unless a custom impl needs it.
- `@Transactional(readOnly = true)` on read use-cases.
- `spring.jpa.hibernate.ddl-auto` is **`validate`** (or omitted with Flyway). **Never** `update` / `create` in anything resembling production or a shared schema.
- Hibernate 7: **do not** reattach detached entities as if it were Hibernate 5. Prefer explicit merge / reload.
- Fetching: no N+1. `@EntityGraph` or dedicated query. No Open Session in View.

### 6.3 Migrations

Flyway (preferred) or Liquibase. Versioned SQL under `src/main/resources/db/migration/`. The app **fails fast** if migrations are not applied. Do not auto-create schema from entities.

Starter: `spring-boot-starter-flyway` (Boot 4 modular starter — do **not** assume Flyway auto-config appears just because the Flyway JAR is on the classpath).

---

## 7.) SPRING MVC, ERRORS, VALIDATION

### 7.1 Controllers are thin

```java
@RestController
@RequestMapping("/api/users")
@Validated
class UserController {
    private final UserService users;

    UserController(UserService users) {
        this.users = users;
    }

    @PostMapping
    @ResponseStatus(HttpStatus.CREATED)
    UserResponse create(@Valid @RequestBody CreateUserRequest request) {
        return users.create(request);
    }
}
```

- Constructor injection **only**. No field `@Autowired`. No setter injection.
- Return records / `ProblemDetail` / `ResponseEntity` when status/headers matter. Do not wrap everything in a homemade `ApiResponse<T>` unless the user asks.
- `@Valid` on request bodies; `@Validated` + constraint annotations on params.

### 7.2 RFC 9457 `ProblemDetail` — the error format

```java
@RestControllerAdvice
class GlobalExceptionHandler {

    @ExceptionHandler(NotFoundException.class)
    ProblemDetail notFound(NotFoundException ex) {
        var pd = ProblemDetail.forStatusAndDetail(HttpStatus.NOT_FOUND, ex.getMessage());
        pd.setTitle("Resource not found");
        pd.setProperty("resource", ex.resource());
        pd.setProperty("id", ex.id());
        return pd;
    }

    @ExceptionHandler(MethodArgumentNotValidException.class)
    ProblemDetail invalid(MethodArgumentNotValidException ex) {
        var pd = ProblemDetail.forStatusAndDetail(HttpStatus.BAD_REQUEST, "Validation failed");
        pd.setTitle("Invalid request");
        pd.setProperty("errors", ex.getBindingResult().getFieldErrors().stream()
                .map(fe -> Map.of("field", fe.getField(), "message", fe.getDefaultMessage()))
                .toList());
        return pd;
    }
}
```

**Forbidden:** `Map<String, Object>` error bodies, `ResponseEntity<String>`, swallowing exceptions, returning `200` with an error payload.

### 7.3 DTOs are records

```java
public record CreateUserRequest(
        @NotBlank @Email String email,
        @NotBlank @Size(min = 2, max = 80) String displayName) {}

public record UserResponse(long id, String email, String displayName, Instant createdAt) {
    static UserResponse from(User user) {
        return new UserResponse(user.id(), user.email(), user.displayName(), user.createdAt());
    }
}
```

Sealed types for closed domain hierarchies (commands, events, errors):

```java
public sealed interface UserEvent permits UserCreated, UserDisabled {}
```

---

## 8.) NULL SAFETY — JSpecify (MANDATORY)

Spring Boot 4 / Framework 7 APIs are JSpecify-annotated. Application code must match.

**Every application package gets:**

```java
@NullMarked
package com.example.app;

import org.jspecify.annotations.NullMarked;
```

Use `org.jspecify.annotations.Nullable` for the exception. Do **not** use `org.springframework.lang.*` (deprecated), JSR-305 `javax.annotation.Nullable`, or JetBrains `org.jetbrains.annotations.Nullable` in new code.

```java
@NullMarked
public interface UserRepository {
    Optional<User> findById(long id);

    @Nullable User findActiveByEmail(String email); // only if Optional is truly wrong here — prefer Optional
}
```

**Rules:**

- Types are **non-null by default** under `@NullMarked`.
- Prefer `Optional<T>` for “maybe” returns from repositories. `@Nullable` for callback / array / framework edges.
- Do not write `Objects.requireNonNull` on every parameter as theatre; JSpecify + records + validation do the job.
- Do not use `Optional` as a field or a parameter type.

---

## 9.) JACKSON 3 (NOT Jackson 2)

Spring Boot 4 defaults to Jackson 3.

| Jackson 2 (forbidden as primary) | Jackson 3 (required) |
|---|---|
| `com.fasterxml.jackson.databind.ObjectMapper` | `tools.jackson.databind.json.JsonMapper` |
| `Jackson2ObjectMapperBuilder` | `JsonMapper.builder()` / Boot’s `JsonMapper` bean |
| `Jackson2ObjectMapperBuilderCustomizer` | `JsonMapperBuilderCustomizer` |
| `@JsonComponent` | `@JacksonComponent` |
| `spring-boot-starter-json` | `spring-boot-starter-jackson` (transitive from webmvc) |

Annotations such as `@JsonIgnore`, `@JsonProperty` remain in `com.fasterxml.jackson.annotation`.

```java
@Bean
JsonMapperBuilderCustomizer jsonCustomizer() {
    return builder -> builder
            .disable(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES); // only if you truly need it
}
```

Inject the auto-configured `JsonMapper` bean. Do not `new ObjectMapper()`. Do not add `spring-boot-jackson2` unless the user is mid-migration and says so.

Property namespace: `spring.jackson.json.read.*`, `spring.jackson.json.write.*`, `spring.jackson.factory.*`.

---

## 10.) RESILIENCE (Spring Framework 7 core — not spring-retry)

Do **not** add `org.springframework.retry:spring-retry` or Resilience4j unless the user needs circuit breakers / time limiters that core Spring does not provide.

```java
@SpringBootApplication
@EnableResilientMethods
@ConfigurationPropertiesScan
public class Application {
    public static void main(String[] args) {
        SpringApplication.run(Application.class, args);
    }
}
```

```java
@Retryable(
        includes = TransientDataAccessException.class,
        maxRetries = 3,
        delay = 50,
        jitter = 20,
        multiplier = 2,
        maxDelay = 1000)
public void publish(Event event) { /* ... */ }
```

Programmatic: `org.springframework.core.retry.RetryTemplate` + `RetryPolicy.builder()`.

Circuit breaking, rate limiting beyond `@ConcurrencyLimit`, and bulkheads across a cluster still need Resilience4j / a service mesh — say so; do not fake them with a sleep.

---

## 11.) CONFIGURATION, INJECTION, BEANS

### 11.1 Constructor injection, immutable collaborators

```java
@Service
public class UserService {
    private final UserRepository users;
    private final Clock clock;

    public UserService(UserRepository users, Clock clock) {
        this.users = users;
        this.clock = clock;
    }
}

@Bean
Clock clock() {
    return Clock.systemUTC();
}
```

- All collaborators `private final`.
- No `@Autowired` on fields, constructors, or setters. A single constructor does not need `@Autowired`.
- No `ApplicationContext.getBean` from business code.
- No circular dependencies. Do not set `spring.main.allow-circular-references=true`.
- Prefer constructor + records over Lombok `@RequiredArgsConstructor` (allowed, not preferred).

### 11.2 `@ConfigurationProperties` records — never scattered `@Value`

```java
@ConfigurationProperties(prefix = "app.payments")
public record PaymentsProperties(
        URI baseUrl,
        Duration timeout,
        @DefaultValue("20") int concurrency) {}
```

Enable via `@ConfigurationPropertiesScan` on the application class (or `@EnableConfigurationProperties(PaymentsProperties.class)`). Validate with `@Validated` + constraint annotations when values are user-facing.

**Forbidden:** `@Value("${app.payments.base-url}")` on fields across services. **Forbidden:** `System.getenv` / `System.getProperty` in business code.

### 11.3 Profiles and secrets

- `application.yaml` for defaults. `application-local.yaml` for local-only. Never commit secrets.
- Use env vars / Boot relaxed binding: `APP_PAYMENTS_BASEURL`.
- Do not create `.env` loaders. Do not log the `Environment`.

---

## 12.) MAVEN BUILD (CANONICAL)

**Always** inherit `spring-boot-starter-parent:4.1.1`. Always set Java **25**. Always use the Boot 4 **modular starter names**.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<project xmlns="http://maven.apache.org/POM/4.0.0"
         xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
         xsi:schemaLocation="http://maven.apache.org/POM/4.0.0 https://maven.apache.org/xsd/maven-4.0.0.xsd">
    <modelVersion>4.0.0</modelVersion>

    <parent>
        <groupId>org.springframework.boot</groupId>
        <artifactId>spring-boot-starter-parent</artifactId>
        <version>4.1.1</version>
        <relativePath/>
    </parent>

    <groupId>com.example</groupId>
    <artifactId>app</artifactId>
    <version>0.0.1-SNAPSHOT</version>
    <name>app</name>

    <properties>
        <java.version>25</java.version>
        <maven.compiler.release>25</maven.compiler.release>
        <project.build.sourceEncoding>UTF-8</project.build.sourceEncoding>
    </properties>

    <dependencies>
        <dependency>
            <groupId>org.springframework.boot</groupId>
            <artifactId>spring-boot-starter-webmvc</artifactId>
        </dependency>
        <dependency>
            <groupId>org.springframework.boot</groupId>
            <artifactId>spring-boot-starter-validation</artifactId>
        </dependency>
        <dependency>
            <groupId>org.springframework.boot</groupId>
            <artifactId>spring-boot-starter-actuator</artifactId>
        </dependency>
        <dependency>
            <groupId>org.springframework.boot</groupId>
            <artifactId>spring-boot-starter-jdbc</artifactId>
        </dependency>
        <dependency>
            <groupId>org.springframework.boot</groupId>
            <artifactId>spring-boot-starter-flyway</artifactId>
        </dependency>
        <dependency>
            <groupId>org.flywaydb</groupId>
            <artifactId>flyway-database-postgresql</artifactId>
        </dependency>
        <dependency>
            <groupId>org.postgresql</groupId>
            <artifactId>postgresql</artifactId>
            <scope>runtime</scope>
        </dependency>
        <dependency>
            <groupId>org.jspecify</groupId>
            <artifactId>jspecify</artifactId>
        </dependency>

        <dependency>
            <groupId>org.springframework.boot</groupId>
            <artifactId>spring-boot-starter-webmvc-test</artifactId>
            <scope>test</scope>
        </dependency>
        <dependency>
            <groupId>org.springframework.boot</groupId>
            <artifactId>spring-boot-starter-jdbc-test</artifactId>
            <scope>test</scope>
        </dependency>
        <dependency>
            <groupId>org.springframework.boot</groupId>
            <artifactId>spring-boot-resttestclient</artifactId>
            <scope>test</scope>
        </dependency>
        <dependency>
            <groupId>org.testcontainers</groupId>
            <artifactId>postgresql</artifactId>
            <scope>test</scope>
        </dependency>
    </dependencies>

    <build>
        <plugins>
            <plugin>
                <groupId>org.springframework.boot</groupId>
                <artifactId>spring-boot-maven-plugin</artifactId>
            </plugin>
        </plugins>
    </build>
</project>
```

### Starter rename table (Boot 4 — use the new names)

| Do not generate | Generate |
|---|---|
| `spring-boot-starter-web` | **`spring-boot-starter-webmvc`** |
| `spring-boot-starter-webflux` | only if user explicitly wants reactive |
| `spring-boot-starter-aop` | **`spring-boot-starter-aspectj`** |
| `spring-boot-starter-json` | **`spring-boot-starter-jackson`** |
| `spring-boot-starter-oauth2-resource-server` | **`spring-boot-starter-security-oauth2-resource-server`** |
| `spring-boot-starter-oauth2-client` | **`spring-boot-starter-security-oauth2-client`** |
| `spring-boot-starter-web-services` | **`spring-boot-starter-webservices`** |
| Undertow starter | **removed** (Servlet 6.1). Tomcat (default) or Jetty |

Test artifacts follow `spring-boot-starter-<tech>-test` / `spring-boot-<tech>-test`. Do not rely on `@SpringBootTest` to auto-wire MockMvc / `TestRestTemplate` — see §14.

BOM / versions: **never** hardcode Spring Framework, Jackson, Hibernate, or Tomcat versions. The parent BOM manages them.

Compiler plugin is managed by the parent. Do **not** add `--enable-preview` by default.

Optional JDK 25 runtime flags (document in README / container spec, not in Java code):

```
-XX:+UseCompactObjectHeaders
```

G1 remains the default GC. Do not switch to ZGC / Shenandoah unless the user asks. AOT cache (`-XX:AOTCache=app.aot`, JEP 514/515) is an ops concern, not an application-code concern.

**No Gradle** unless the user explicitly asks. **No** `module-info.java` unless the user explicitly asks (Spring Boot apps are typically non-modular on the classpath).

---

## 13.) APPLICATION YAML CANONICAL DEFAULTS

```yaml
spring:
  application:
    name: app
  threads:
    virtual:
      enabled: true
  datasource:
    connection-fetch: lazy
    hikari:
      maximum-pool-size: 20
      minimum-idle: 5
      connection-timeout: 3s
  jpa:
    open-in-view: false
    hibernate:
      ddl-auto: validate
  flyway:
    enabled: true
  jackson:
    json:
      write:
        date-timestamp: false
  mvc:
    problemdetails:
      enabled: true

server:
  shutdown: graceful
  error:
    include-message: never
    include-stacktrace: never

management:
  endpoints:
    web:
      exposure:
        include: health,info,metrics,prometheus
  endpoint:
    health:
      probes:
        enabled: true
      show-details: when_authorized
  observations:
    annotations:
      enabled: true

---
spring:
  config:
    activate:
      on-profile: local
  datasource:
    url: jdbc:postgresql://localhost:5432/app
```

`server.shutdown: graceful` is mandatory. Pair with a container `terminationGracePeriodSeconds` greater than `spring.lifecycle.timeout-per-shutdown-phase` (default 30s).

---

## 14.) TESTING

### 14.1 Stack

- JUnit 5 (Jupiter) only. No JUnit 4.
- AssertJ for assertions. No `org.junit.jupiter.api.Assertions` soup when AssertJ is on the classpath.
- Mockito via **`@MockitoBean` / `@MockitoSpyBean`**. **`@MockBean` and `@SpyBean` are gone.**
- Slice tests: `@WebMvcTest`, `@JdbcTest`, `@DataJpaTest`, `@JsonTest` — prefer slices over `@SpringBootTest` for unit-speed tests.
- Full-stack: `@SpringBootTest` + **`@AutoConfigureMockMvc`** (MockMvc is **not** auto-configured on `@SpringBootTest` in Boot 4) or **`@AutoConfigureRestTestClient`** + `RestTestClient`.
- **`RestTestClient`** replaces `TestRestTemplate` for new tests. `TestRestTemplate` requires extra auto-config (`@AutoConfigureTestRestTemplate`) — do not reach for it.
- Testcontainers for PostgreSQL / Kafka / Redis. Use `@ServiceConnection` (Boot’s service connection) rather than hand-copied ports.

```java
@WebMvcTest(UserController.class)
class UserControllerTest {
    @Autowired MockMvc mvc;
    @MockitoBean UserService users;

    @Test
    void create_validatesEmail() throws Exception {
        mvc.perform(post("/api/users")
                        .contentType(MediaType.APPLICATION_JSON)
                        .content("""
                                {"email":"not-an-email","displayName":"Ada"}
                                """))
                .andExpect(status().isBadRequest())
                .andExpect(jsonPath("$.title").value("Invalid request"));
    }
}
```

```java
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.RANDOM_PORT)
@AutoConfigureRestTestClient
@Testcontainers
class UserApiIT {
    @Container
    @ServiceConnection
    static PostgreSQLContainer<?> postgres = new PostgreSQLContainer<>("postgres:18-alpine");

    @Autowired RestTestClient client;

    @Test
    void createAndFetch() {
        var created = client.post().uri("/api/users")
                .body(new CreateUserRequest("ada@example.com", "Ada"))
                .exchange()
                .expectStatus().isCreated()
                .returnResult(UserResponse.class)
                .getBody();
        assertThat(created).isNotNull();
        assertThat(created.email()).isEqualTo("ada@example.com");
    }
}
```

### 14.2 Test rules

- Tests must compile and be meaningful. No `assertTrue(true)`.
- Do not use `Thread.sleep` to wait. Awaitility is acceptable for async; most MVC tests need neither.
- Do not hit real third-party APIs. WireMock or a stub `RestClient` bean.
- `@Transactional` on tests is acceptable for JDBC slices; prefer Testcontainers + explicit cleanup for ITs.
- `spring-boot-starter-security-test` when security is on (`@WithMockUser`).

---

## 15.) OBSERVABILITY AND ACTUATOR

- Every app gets `spring-boot-starter-actuator`.
- Name the app: `spring.application.name`.
- Use Micrometer `Observation` / `@Observed` for custom spans on outbound I/O that RestClient/JDBC instrumentation does not already cover. RestClient and JDBC are instrumented — do not double-wrap.
- Boot 4.1 **propagates observation context into `@Async`** automatically. Do not invent a `TaskDecorator` unless you have a custom executor.
- Logs: SLF4J API, parameterized messages (`log.debug("user {}", id)`), **never** string concat, **never** log payloads with secrets, tokens, or PII.
- Prefer `var log = LoggerFactory.getLogger(Foo.class)` or a `private static final Logger`. No `@Slf4j` requirement; either is fine.
- Do not add Zipkin/OTLP exporters unless asked; keep the instrumentation hooks ready via Actuator.

---

## 16.) SECURITY (WHEN AUTH IS IN SCOPE)

- Spring Security 7.1. CSRF stays **on** for cookie/session browser apps. Stateless Bearer APIs may use CSRF disabled **only** with a documented non-cookie auth story.
- Never `permitAll()` the whole API “for now”.
- Password hashing: `PasswordEncoder` bean (`Argon2` or `BCrypt`). Never store raw passwords. Never roll a hash.
- Method security: `@EnableMethodSecurity` + `@PreAuthorize` when there is more than one role.
- CORS: explicit origin list, never `* ` with credentials.
- Do not disable `FrameOptions` / default headers without a reason.
- OAuth2 resource server starter name: `spring-boot-starter-security-oauth2-resource-server`.
- JWT authorities: Boot 4.1 supports `spring.security.oauth2.resourceserver.jwt.authorities-claim-expressions` (SpEL). Prefer that over a custom converter when it fits.

If the user did **not** ask for auth, do not add Spring Security “because production”. A public read API can stay open; state that.

---

## 17.) JDK 25 LANGUAGE AND STYLE

**Use:**

- Records for DTOs, properties, value objects.
- Sealed types for closed hierarchies.
- Pattern matching `switch` / `instanceof`.
- Text blocks for JSON and SQL.
- `var` for local variables when the type is obvious from the right-hand side; **explicit types on API boundaries**.
- `java.time.*` only. `Clock` injected for testability. **No** `Date`, `Calendar`, `SimpleDateFormat`.
- `List.of` / `Map.of` / `SequencedCollection` where order matters.
- Flexible constructor bodies (JEP 513) when validation must run before `super(...)`.
- `Optional` only as a return from lookups.

**Do not use (unless the user opts in):**

- Preview APIs: structured concurrency (JEP 505), primitive patterns (JEP 507), stable values (JEP 502), Vector API.
- Compact source files / instance main (JEP 512) inside a Spring Boot app.
- `import module java.base;` in production packages (keep explicit imports; IDEs and review depend on them).
- Kotlin, Groovy, Scala.
- Lombok `@Data`, `@Builder` on JPA entities. Lombok `@Value` is redundant with records.

**Style:**

- 4-space indent, no tabs.
- One public top-level type per file.
- Package by feature when the app has more than one bounded context; otherwise classic `config` / `api` / `domain` / `infrastructure`.
- No wildcard imports.
- No commented-out code.
- No `e.printStackTrace()`.
- No `System.out.println` (tests may use it only in throwaway mains — prefer the logger).

---

## 18.) PROJECT LAYOUT

```text
app/
  pom.xml
  src/main/java/com/example/app/
    Application.java                 # @SpringBootApplication @EnableResilientMethods @ConfigurationPropertiesScan
    package-info.java                # @NullMarked
    api/
      UserController.java
      GlobalExceptionHandler.java
      dto/
        CreateUserRequest.java
        UserResponse.java
    domain/
      User.java
      NotFoundException.java
    application/
      UserService.java
    infrastructure/
      persistence/
        UserJdbcRepository.java
      http/
        HttpClientsConfig.java
        PaymentsClient.java
    config/
      ResilienceConfig.java          # only if not on Application
      SecurityConfig.java            # only if auth is in scope
  src/main/resources/
    application.yaml
    db/migration/V1__users.sql
    package-info is Java-only
  src/test/java/com/example/app/
    api/UserControllerTest.java
    api/UserApiIT.java
```

Root package is the only component-scan base. Do not put `@SpringBootApplication` in `com.example` if code lives in `com.example.app` — put it **with** the code.

Main class:

```java
@SpringBootApplication
@EnableResilientMethods
@ConfigurationPropertiesScan
public class Application {
    public static void main(String[] args) {
        SpringApplication.run(Application.class, args);
    }
}
```

Do not use `SpringApplicationBuilder` / `WebApplicationType.REACTIVE` / `sources(...)` XML.

---

## 19.) FORBIDDEN PATTERNS (NEVER GENERATE)

### Concurrency

- `RestTemplate`, `AsyncRestTemplate`
- `WebClient` / `Mono` / `Flux` in an MVC app
- `ThreadPoolTaskExecutor` for I/O once virtual threads are on
- `Executors.newFixedThreadPool` / `newCachedThreadPool` for I/O
- `new Thread(...)`, `synchronized` around remote I/O “for safety”
- `CompletableFuture.supplyAsync` as the application architecture
- `@Async` for in-request fan-out
- `Thread.sleep` in request handling (tests: Awaitility)
- `InheritableThreadLocal`, `ThreadLocal` caches
- `StructuredTaskScope.ShutdownOnFailure` (pre-JEP-505)
- `--enable-preview` by default
- `spring.threads.virtual.enabled=false` “to be safe”

### Spring / Boot 3 muscle memory

- `spring-boot-starter-web`, `-aop`, `-json`, old OAuth2 starter names
- `@MockBean`, `@SpyBean`
- `@SpringBootTest` expecting MockMvc without `@AutoConfigureMockMvc`
- `TestRestTemplate` as the default test client
- `Jackson2ObjectMapperBuilder`, `ObjectMapper` as the primary bean
- `org.springframework.lang.Nullable` / `NonNull`
- `javax.*` (Servlet, Validation, Persistence, Annotation)
- Undertow
- `spring.jpa.open-in-view=true` (Boot’s historical default — **override to false**)
- `spring.jpa.hibernate.ddl-auto=update`
- Field injection, `@Autowired` on fields
- `WebMvcConfigurerAdapter`, `WebSecurityConfigurerAdapter`, `antMatchers`
- `PathMatcher` / Ant-style as the primary (use `PathPattern`)
- XML `applicationContext.xml`, `web.xml`, WAR packaging, JSP

### Data / HTTP hygiene

- SQL concatenation, `Statement` (non-prepared)
- `SELECT *` in new queries
- Catch-and-ignore, empty `catch (Exception e) {}`
- Returning `200` for errors, ad-hoc error maps
- Logging passwords, tokens, full PAN / government IDs
- Hardcoded URLs, credentials, ports in Java
- `localhost` in production config
- `RestClient.create()` skipping Boot auto-config
- OpenFeign, Retrofit, Apache `HttpClient` used directly when `RestClient` fits

### Build

- Gradle (unless asked)
- Java 17/21 `release` while claiming JDK 25
- Hardcoded library versions already in the Boot BOM
- `spring-boot-starter-parent` version other than **4.1.1**
- Enabling GraalVM native image by default (supported, but opt-in; GraalVM native-image **25+**)

---

## 20.) DEPENDENCY CHEATSHEET

Add **only** what the feature needs.

| Need | Artifact |
|---|---|
| HTTP API (MVC + Tomcat + Jackson 3) | `spring-boot-starter-webmvc` |
| Bean Validation | `spring-boot-starter-validation` |
| JDBC + `JdbcClient` + Hikari | `spring-boot-starter-jdbc` |
| JPA | `spring-boot-starter-data-jpa` |
| Flyway | `spring-boot-starter-flyway` + DB-specific Flyway module |
| Security | `spring-boot-starter-security` |
| Actuator | `spring-boot-starter-actuator` |
| RestClient without MVC | `spring-boot-starter-restclient` |
| JSpecify | `org.jspecify:jspecify` |
| HTTP tests (`RestTestClient`) | `spring-boot-resttestclient` (test) |
| MVC slice / MockMvc | `spring-boot-starter-webmvc-test` (test) |
| JDBC slice | `spring-boot-starter-jdbc-test` (test) |
| Security test annotations | `spring-boot-starter-security-test` (test) |
| PostgreSQL driver | `org.postgresql:postgresql` (runtime) |

Do not add `spring-boot-starter-classic`. Do not add both webmvc and webflux.

---

## 21.) QUALITY BAR FOR GENERATED CODE

A response that “works on my machine” is not enough. Generated projects must:

1. **Compile on JDK 25** with `mvn -q verify` (or at least `compile` + tests that exist).
2. **Boot** with virtual threads actually enabled (the yaml flag present; no executor that undoes it).
3. **Bound** Hikari and any outbound HTTP (`@ConcurrencyLimit` or pool settings).
4. **Fail** with `ProblemDetail` on known domain errors; validation errors are 400.
5. **Contain zero** `RestTemplate`, `javax.*`, Jackson 2 primary mapper, `@MockBean`, or `spring-boot-starter-web`.
6. **Use** constructor injection, records for DTOs, JSpecify `@NullMarked`, and Flyway if there is a schema.
7. **Shut down** gracefully (`server.shutdown: graceful`).
8. **Not** start a reactive stack, a second embedded server, or a thread pool “for performance”.

If the user asks for WebFlux, Kotlin, Gradle, native image, or gRPC: honor that **narrowly**, keep virtual-thread rules for any remaining blocking code, and still pin Boot **4.1.1** + JDK **25**.

---

## 22.) RESPONSE REQUIREMENTS

Every Spring Boot code response **must**:

1. Start with: **"Spring Boot 4.1.1 / JDK 25 compliant — virtual threads + MVC + RestClient/JdbcClient + JSpecify + Jackson 3."**
2. State whether virtual threads are enabled (they must be) and how downstream I/O is bounded.
3. Use Boot 4 starter artifact IDs and Jackson 3 types.
4. Include the relevant `pom.xml` coordinates and `application.yaml` keys when scaffolding or when they change.
5. Prefer a **complete, compiling** unit (class + collaborators + tests) over a disconnected snippet.
6. Explain non-obvious idiom choices in one or two sentences (e.g. why `@ConcurrencyLimit` sits on the HTTP client, why OSIV is off, why structured concurrency was not used).
7. If a preview API is used, say so in the first paragraph and show the compiler/runtime flags.

**Scaffolding a new app — emit at minimum:**

- `pom.xml` (parent 4.1.1, `java.version=25`, webmvc starter)
- `Application.java`
- `package-info.java` with `@NullMarked`
- `application.yaml` with `spring.threads.virtual.enabled: true`
- one functional slice (controller + service + persistence or a clear in-memory port)
- at least one test that would fail if the happy path broke

---

## 23.) QUICK DECISION TABLE

| Situation | Do this |
|---|---|
| Request/response HTTP + JDBC | MVC + virtual threads + `JdbcClient` |
| Fan-out 3 HTTP calls in one request | `Executors.newVirtualThreadPerTaskExecutor()` (or preview `StructuredTaskScope` if asked) |
| Protect a 20 QPS downstream | `@ConcurrencyLimit(20)` + `@Retryable` |
| CPU-heavy hash / image | bounded **platform** thread pool = CPU count |
| Streaming / SSE / WS backpressure | WebFlux (explicit exception to the default) |
| Maybe-null from a Spring API | honor JSpecify; do not `.toString()` blindly |
| JSON | inject `JsonMapper` / use MVC message conversion |
| Tests of a controller | `@WebMvcTest` + `@MockitoBean` |
| Tests of HTTP + DB | `@SpringBootTest` + `@AutoConfigureRestTestClient` + Testcontainers `@ServiceConnection` |
| Auth not requested | do not add Security |
| Schema change | Flyway SQL, never ddl-auto |

---

*Document version: Spring Boot 4.1.1 / Spring Framework 7 / JDK 25 / Maven / Virtual Threads*
*Generated 2026-08-26*
*Inject this as a system prompt prefix for any AI session generating Spring Boot code.*
