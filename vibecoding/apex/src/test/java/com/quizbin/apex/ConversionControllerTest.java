package com.quizbin.apex;

import static org.springframework.boot.test.context.SpringBootTest.WebEnvironment.RANDOM_PORT;

import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.test.web.server.LocalServerPort;
import org.springframework.http.HttpEntity;
import org.springframework.http.HttpHeaders;
import org.springframework.http.HttpStatusCode;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.client.DefaultResponseErrorHandler;
import org.springframework.web.client.RestTemplate;

@SpringBootTest(webEnvironment = RANDOM_PORT)
class ConversionControllerTest {

    @LocalServerPort
    private int port;

    private final RestTemplate restTemplate;

    ConversionControllerTest() {
        this.restTemplate = new RestTemplate();
        this.restTemplate.setErrorHandler(new DefaultResponseErrorHandler() {
            @Override
            public boolean hasError(HttpStatusCode statusCode) {
                return statusCode.is5xxServerError();
            }
        });
    }

    @Test
    @DisplayName("Converts 0°C to 32°F")
    void convertsZeroCelsius() {
        ResponseEntity<ConvertResponse> response =
                post("/convert/temperature", "{\"celsius\":0}", ConvertResponse.class);

        org.assertj.core.api.Assertions.assertThat(response.getStatusCode().value())
                .isEqualTo(200);
        org.assertj.core.api.Assertions.assertThat(response.getHeaders().getContentType())
                .isEqualTo(MediaType.APPLICATION_JSON);
        ConvertResponse body = response.getBody();
        org.assertj.core.api.Assertions.assertThat(body).isNotNull();
        org.assertj.core.api.Assertions.assertThat(body.celsius()).isEqualTo(0);
        org.assertj.core.api.Assertions.assertThat(body.fahrenheit()).isEqualTo(32.0);
    }

    @Test
    @DisplayName("Converts 100°C to 212°F")
    void convertsBoilingCelsius() {
        ResponseEntity<ConvertResponse> response =
                post("/convert/temperature", "{\"celsius\":100}", ConvertResponse.class);

        org.assertj.core.api.Assertions.assertThat(response.getStatusCode().value())
                .isEqualTo(200);
        ConvertResponse body = response.getBody();
        org.assertj.core.api.Assertions.assertThat(body).isNotNull();
        org.assertj.core.api.Assertions.assertThat(body.celsius()).isEqualTo(100);
        org.assertj.core.api.Assertions.assertThat(body.fahrenheit()).isEqualTo(212.0);
    }

    @Test
    @DisplayName("Converts -40°C to -40°F")
    void convertsNegativeForty() {
        ResponseEntity<ConvertResponse> response =
                post("/convert/temperature", "{\"celsius\":-40}", ConvertResponse.class);

        org.assertj.core.api.Assertions.assertThat(response.getStatusCode().value())
                .isEqualTo(200);
        ConvertResponse body = response.getBody();
        org.assertj.core.api.Assertions.assertThat(body).isNotNull();
        org.assertj.core.api.Assertions.assertThat(body.celsius()).isEqualTo(-40);
        org.assertj.core.api.Assertions.assertThat(body.fahrenheit()).isEqualTo(-40.0);
    }

    @Test
    @DisplayName("Converts 32°F to 0°C")
    void convertsFreezingFahrenheit() {
        ResponseEntity<ConvertResponse> response =
                post("/convert/temperature", "{\"fahrenheit\":32}", ConvertResponse.class);

        org.assertj.core.api.Assertions.assertThat(response.getStatusCode().value())
                .isEqualTo(200);
        org.assertj.core.api.Assertions.assertThat(response.getHeaders().getContentType())
                .isEqualTo(MediaType.APPLICATION_JSON);
        ConvertResponse body = response.getBody();
        org.assertj.core.api.Assertions.assertThat(body).isNotNull();
        org.assertj.core.api.Assertions.assertThat(body.celsius()).isEqualTo(0);
        org.assertj.core.api.Assertions.assertThat(body.fahrenheit()).isEqualTo(32.0);
    }

    @Test
    @DisplayName("Converts 212°F to 100°C")
    void convertsBoilingFahrenheit() {
        ResponseEntity<ConvertResponse> response =
                post("/convert/temperature", "{\"fahrenheit\":212}", ConvertResponse.class);

        org.assertj.core.api.Assertions.assertThat(response.getStatusCode().value())
                .isEqualTo(200);
        ConvertResponse body = response.getBody();
        org.assertj.core.api.Assertions.assertThat(body).isNotNull();
        org.assertj.core.api.Assertions.assertThat(body.celsius()).isEqualTo(100);
        org.assertj.core.api.Assertions.assertThat(body.fahrenheit()).isEqualTo(212.0);
    }

    @Test
    @DisplayName("Converts -40°F to -40°C")
    void convertsNegativeFortyFahrenheit() {
        ResponseEntity<ConvertResponse> response =
                post("/convert/temperature", "{\"fahrenheit\":-40}", ConvertResponse.class);

        org.assertj.core.api.Assertions.assertThat(response.getStatusCode().value())
                .isEqualTo(200);
        ConvertResponse body = response.getBody();
        org.assertj.core.api.Assertions.assertThat(body).isNotNull();
        org.assertj.core.api.Assertions.assertThat(body.celsius()).isEqualTo(-40);
        org.assertj.core.api.Assertions.assertThat(body.fahrenheit()).isEqualTo(-40.0);
    }

    @Test
    @DisplayName("Returns 400 when neither celsius nor fahrenheit is provided")
    void returnsBadRequestWhenNeitherProvided() {
        ResponseEntity<String> response = post("/convert/temperature", "{}", String.class);

        org.assertj.core.api.Assertions.assertThat(response.getStatusCode().value())
                .isEqualTo(400);
    }

    @Test
    @DisplayName("Returns 400 when both celsius and fahrenheit are provided")
    void returnsBadRequestWhenBothProvided() {
        ResponseEntity<String> response =
                post("/convert/temperature", "{\"celsius\":0,\"fahrenheit\":32}", String.class);

        org.assertj.core.api.Assertions.assertThat(response.getStatusCode().value())
                .isEqualTo(400);
    }

    @Test
    @DisplayName("Returns 400 when celsius is below absolute zero")
    void returnsBadRequestWhenCelsiusBelowAbsoluteZero() {
        ResponseEntity<String> response = post("/convert/temperature", "{\"celsius\":-300}", String.class);

        org.assertj.core.api.Assertions.assertThat(response.getStatusCode().value())
                .isEqualTo(400);
        org.assertj.core.api.Assertions.assertThat(response.getBody()).contains("below absolute zero");
    }

    @Test
    @DisplayName("Returns 400 when fahrenheit is below absolute zero")
    void returnsBadRequestWhenFahrenheitBelowAbsoluteZero() {
        ResponseEntity<String> response = post("/convert/temperature", "{\"fahrenheit\":-500}", String.class);

        org.assertj.core.api.Assertions.assertThat(response.getStatusCode().value())
                .isEqualTo(400);
        org.assertj.core.api.Assertions.assertThat(response.getBody()).contains("below absolute zero");
    }

    private <T> ResponseEntity<T> post(String path, String json, Class<T> responseType) {
        HttpHeaders headers = new HttpHeaders();
        headers.setContentType(MediaType.APPLICATION_JSON);
        HttpEntity<String> entity = new HttpEntity<>(json, headers);
        return restTemplate.postForEntity(baseUrl() + path, entity, responseType);
    }

    private String baseUrl() {
        return "http://localhost:" + port;
    }
}
