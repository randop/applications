package com.quizbin.mp;

import java.util.logging.Logger;

import com.icegreen.greenmail.util.GreenMail;
import com.icegreen.greenmail.util.ServerSetupTest;

import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.enterprise.context.Initialized;
import jakarta.enterprise.event.Observes;

@ApplicationScoped
public class MailServer {
    private static final Logger log = Logger.getLogger(MailServer.class.getName());
    private GreenMail greenMail;

    // This forces eager instantiation + runs @PostConstruct at startup
    protected void onApplicationInitialized(@Observes @Initialized(ApplicationScoped.class) Object event) {
        log.info("MailServer initialized");
    }

    @PostConstruct
    void start() {
        greenMail = new GreenMail(ServerSetupTest.IMAP);
        greenMail.start();

        // Optional: create test user
        greenMail.setUser("test@example.com", "testuser", "secret123");

        log.info("IMAP started on port " + greenMail.getImap().getPort());
    }

    @PreDestroy
    void stop() {
        if (greenMail != null) {
            greenMail.stop();
        }
    }
}
