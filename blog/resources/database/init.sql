ALTER DATABASE mydb
SET
    timezone TO 'UTC';

CREATE TABLE IF NOT EXISTS modes (
    id serial PRIMARY KEY,
    created_at timestamp DEFAULT current_timestamp,
    updated_at timestamp DEFAULT current_timestamp,
    mode varchar(25) NOT NULL
);

INSERT INTO
    modes (id, mode)
VALUES
    (1, 'markdown'),
    (2, 'html');

GRANT ALL PRIVILEGES ON TABLE modes TO myuser;

CREATE TABLE IF NOT EXISTS posts (
    id SERIAL PRIMARY KEY,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    title VARCHAR(255) NOT NULL,
    content TEXT NOT NULL,
    mode_id INTEGER NOT NULL DEFAULT 1,
    CONSTRAINT fk_mode_id FOREIGN KEY (mode_id) REFERENCES modes (id)
);

GRANT ALL PRIVILEGES ON TABLE posts TO myuser;

INSERT INTO
    posts (
        id,
        created_at,
        updated_at,
        title,
        "content",
        mode_id
    )
VALUES
    (
        1,
        '2025-06-12 11:11:55.168',
        '2025-06-12 11:11:55.168',
        'My first post',
        'Lorem ipsum dolor sit amet, consectetur adipiscing elit. Curabitur eu ultrices ligula. Maecenas id diam a elit viverra varius non quis ipsum. Donec interdum erat massa, in bibendum enim dignissim sed. Praesent viverra egestas ex id semper. Etiam molestie metus justo. Interdum et malesuada fames ac ante ipsum primis in faucibus. Phasellus maximus purus eget consequat pulvinar. Curabitur volutpat metus in erat vestibulum, vitae sollicitudin velit hendrerit. Pellentesque suscipit iaculis felis, nec volutpat enim commodo in. Aenean eu vehicula tellus, eget ullamcorper nisl. Phasellus ut arcu ac justo interdum ultricies. Duis auctor mauris in aliquet vehicula. Fusce sit amet mi diam. Vestibulum turpis velit, ultrices et luctus vel, interdum in risus. Integer metus metus, pretium nec eros maximus, laoreet tincidunt nisl. Donec eu rutrum lorem.

Vestibulum tortor odio, congue fringilla magna in, convallis molestie dui. Donec quis erat egestas, aliquet neque in, dignissim quam. Proin semper tellus at vestibulum euismod. Duis sollicitudin malesuada libero sed cursus. Fusce eget justo dolor. Etiam sodales tellus id metus ullamcorper, non molestie elit porta. Mauris aliquet sagittis sodales. Mauris vitae varius turpis. Proin hendrerit egestas pellentesque. Cras fermentum et purus in placerat. Phasellus vestibulum elit laoreet nibh tristique, non faucibus erat ultricies. Pellentesque ut fringilla sapien. Cras maximus malesuada quam quis blandit. Donec hendrerit molestie diam et consequat.

```cpp
#include <iostream>

int main() {
    std::cout << "Hello World!!!" << std::endl;
    return 0;
}
```

> Nulla rhoncus turpis sed purus mollis posuere. Aliquam erat volutpat. Morbi tincidunt arcu est, sit amet placerat nibh vehicula eu. Nunc eu egestas velit, eget imperdiet sem. Maecenas sed eleifend leo, sed eleifend massa. Nullam auctor ante sed diam sagittis pretium. Aliquam et sem sapien. Nulla congue risus id consequat posuere. Donec non lectus vel lorem tincidunt facilisis. Quisque leo magna, suscipit consectetur mollis eget, commodo eu dui. Phasellus feugiat malesuada diam, at elementum nunc tempus ut. Nulla tellus nunc, interdum nec efficitur ut, finibus eu mi.
',
        1
    );
