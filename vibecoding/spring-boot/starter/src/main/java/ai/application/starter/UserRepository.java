package ai.application.starter;

import java.util.List;
import org.springframework.data.mongodb.repository.MongoRepository;
import org.springframework.stereotype.Repository;

@Repository
public interface UserRepository extends MongoRepository<User, String> {
    List<User> findByName(String name);

    List<User> findByEmailContaining(String keyword);
}
