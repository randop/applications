package com.quizbin.mp;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.is;

import org.junit.jupiter.api.Test;

import io.helidon.microprofile.testing.junit5.HelidonTest;
import jakarta.inject.Inject;
import jakarta.ws.rs.client.WebTarget;

@HelidonTest
class MainTest {

    @Inject
    private WebTarget target;

    @Test
    void testGreet() {
        Message message = target.path("simple-greet").request().get(Message.class);
        assertThat(message.getMessage(), is("Hello World!"));
    }
}
