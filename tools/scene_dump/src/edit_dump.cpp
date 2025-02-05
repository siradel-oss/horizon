#include <hrz_migration.h>
#include <hrz_protocol_all.h>
#include <hrz_scene_dump_utils.h>

#include <ctype.h>
#include <fmt/format.h>

#include <stdio.h>

#define CLEAR_SCREEN "\033[2J\033[1:1H"

void flush_stdin()
{
    char c;
    while ((c = getchar()) != '\n' && c != EOF)
        ;
}

int read_int(const char* input)
{
    int value = -1;
    printf("%s: ", input);
    fflush(stdout);
    scanf("%d", &value);
    flush_stdin(); // stdin still contains \n
    return value;
}

void read_str(const char* input, char* buffer, int length)
{
    printf("%s: ", input);
    fflush(stdout);
    fgets(buffer, length - 1, stdin);

    // Trim the last \n
    int len = strlen(buffer);
    while (len > 0 && isspace(buffer[len - 1]))
        len -= 1;
    buffer[len] = '\0';
}

struct Menu
{
    std::string title;
    std::vector<std::pair<std::string, std::function<void()>>> actions;

    explicit Menu(const std::string& title) : title{title} {}

    void add_action(const std::string& name, const std::function<void()>& action)
    {
        actions.emplace_back(name, std::move(action));
    }

    void execute()
    {
        puts(CLEAR_SCREEN);
        printf("%s\n", title.c_str());

        for (unsigned int i = 0; i < actions.size(); ++i)
        {
            printf("[%d] %s\n", i, actions[i].first.c_str());
        }

        int action = read_int("Select action");
        if (action >= 0 && action < (int)actions.size())
        {
            actions[action].second();
        }
    }
};

void layers_menu(hrz_proto::SceneDump& dump, bool* has_unsaved_changes)
{
    bool back = false;
    while (!back)
    {
        Menu menu("Edit layers");
        menu.add_action("Back to main menu", [&back]() { back = true; });

        for (int i = 0; i < dump.layers_size(); ++i)
        {
            menu.add_action(
                fmt::format("Delete layer \"{}\"", dump.layers(i).name()),
                [&dump, i, has_unsaved_changes]()
                {
                    dump.mutable_layers()->DeleteSubrange(i, 1);
                    *has_unsaved_changes = true;
                });
        }

        for (int i = 0; i < dump.layers_size(); ++i)
        {
            menu.add_action(
                fmt::format("Rename layer \"{}\"", dump.layers(i).name()),
                [&dump, i, has_unsaved_changes]()
                {
                    char buffer[128];
                    read_str("New name", buffer, 128);
                    dump.mutable_layers(i)->set_name(buffer);
                    *has_unsaved_changes = true;
                });
        }

        menu.execute();
    }
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        printf("Usage: %s <dump to edit>\n", argv[0]);
        return 1;
    }

    const char* input_file = argv[1];
    auto data = hrz::scene_dump::read_file(input_file);
    if (data.empty())
    {
        printf("Error when opening file %s\n", input_file);
        return 1;
    }

    int migrations_applied = 0;
    data = hrz::migration::migrate(data, &migrations_applied);
    if (data.empty())
    {
        printf("Error when migrating file %s\n", input_file);
        return 1;
    }

    if (migrations_applied > 0)
    {
        printf("The scene has been migrated to the latest version.\n");
        printf("%d migration(s) have been applied.\n", migrations_applied);
        printf("Press any key to continue...");
        getchar();
    }

    hrz_proto::SceneDump dump;
    if (!dump.ParseFromArray(data.data(), data.size()))
    {
        printf("Error when decoding %s\n", input_file);
        return 1;
    }

    bool quit = false;
    bool has_unsaved_changes = migrations_applied > 0;
    while (!quit)
    {
        Menu main_menu(fmt::format(
            "Edit scene dump \"{}\"{}", dump.name(), has_unsaved_changes ? " (UNSAVED)" : ""));

        main_menu.add_action(
            "Quit",
            [&quit, has_unsaved_changes]()
            {
                if (has_unsaved_changes)
                {
                    printf("You have unsaved changes. Quit anyway? y/n: ");
                    char c = getchar();
                    if (c != 'y' && c != 'Y') return;
                }
                quit = true;
            });

        main_menu.add_action(
            "Save",
            [&dump, &has_unsaved_changes, input_file]()
            {
                auto data = dump.SerializeAsString();
                hrz::scene_dump::write_file(
                    input_file, {(const std::byte*)data.data(), data.size()});
                has_unsaved_changes = false;
            });

        main_menu.add_action(
            "Rename",
            [&dump, &has_unsaved_changes]()
            {
                char buffer[128];
                read_str("New name", buffer, 128);
                dump.set_name(buffer);
                has_unsaved_changes = true;
            });

        if (dump.layers_size() > 0)
        {
            main_menu.add_action(
                fmt::format("Layers ({})...", dump.layers_size()),
                [&dump, &has_unsaved_changes]() { layers_menu(dump, &has_unsaved_changes); });
        }

        if (dump.has_scene_settings())
        {
            main_menu.add_action(
                "Delete scene settings",
                [&dump, &has_unsaved_changes]()
                {
                    dump.clear_scene_settings();
                    has_unsaved_changes = true;
                });
        }

        for (int i = 0; i < dump.scene_view_settings_size(); ++i)
        {
            int view_index = dump.scene_view_settings(i).index();
            main_menu.add_action(
                fmt::format("Delete scene view settings #{}", view_index),
                [&dump, i, &has_unsaved_changes]()
                {
                    dump.mutable_scene_view_settings()->DeleteSubrange(i, 1);
                    has_unsaved_changes = true;
                });
        }

        for (int i = 0; i < dump.camera_settings_size(); ++i)
        {
            int view_index = dump.camera_settings(i).index();
            main_menu.add_action(
                fmt::format("Delete camera settings #{}", view_index),
                [&dump, i, &has_unsaved_changes]()
                {
                    dump.mutable_camera_settings()->DeleteSubrange(i, 1);
                    has_unsaved_changes = true;
                });
        }

        for (int i = 0; i < dump.cameras_size(); ++i)
        {
            int view_index = dump.cameras(i).index();
            main_menu.add_action(
                fmt::format("Delete initial camera #{}", view_index),
                [&dump, i, &has_unsaved_changes]()
                {
                    dump.mutable_cameras()->DeleteSubrange(i, 1);
                    has_unsaved_changes = true;
                });
        }

        main_menu.execute();
    }

    return 0;
}
