package com.quizbin.apex;

import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.ExceptionHandler;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RestController;

@RestController
public class ConversionController {

    private static final double ABSOLUTE_ZERO_CELSIUS = -273.15;
    private static final double ABSOLUTE_ZERO_FAHRENHEIT = -459.67;

    @PostMapping("/convert/temperature")
    public ResponseEntity<ConvertResponse> convert(@RequestBody ConvertRequest request) {

        Double celsius = request.celsius();
        Double fahrenheit = request.fahrenheit();

        if (celsius == null && fahrenheit == null) {
            throw new IllegalArgumentException("Either celsius or fahrenheit must be provided");
        }

        if (celsius != null && fahrenheit != null) {
            throw new IllegalArgumentException("Provide only one of celsius or fahrenheit");
        }

        if (celsius != null) {
            if (celsius < ABSOLUTE_ZERO_CELSIUS) {
                throw new IllegalArgumentException(
                        "Celsius cannot be below absolute zero (" + ABSOLUTE_ZERO_CELSIUS + "°C)");
            }
            double f = celsius * 9 / 5 + 32;
            return ResponseEntity.ok(new ConvertResponse(celsius, f));
        }

        if (fahrenheit < ABSOLUTE_ZERO_FAHRENHEIT) {
            throw new IllegalArgumentException(
                    "Fahrenheit cannot be below absolute zero (" + ABSOLUTE_ZERO_FAHRENHEIT + "°F)");
        }
        double c = (fahrenheit - 32) * 5 / 9;
        return ResponseEntity.ok(new ConvertResponse(c, fahrenheit));
    }

    @ExceptionHandler(IllegalArgumentException.class)
    public ResponseEntity<String> handleIllegalArgument(IllegalArgumentException ex) {
        return ResponseEntity.status(HttpStatus.BAD_REQUEST).body(ex.getMessage());
    }
}
