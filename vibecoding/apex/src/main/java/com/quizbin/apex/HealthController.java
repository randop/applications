package com.quizbin.apex;

import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RestController;

@RestController
public class HealthController {

    @GetMapping("/")
    public String status() {
        return "OK";
    }

    @GetMapping("/health")
    public String health() {
        return "OK";
    }
}
