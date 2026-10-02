# Let people add your app to Harpoon with a QR code

Harpoon users can add an app by scanning a QR code on its web page or README, instead of
typing the repository URL on the phone. Harpoon's scanner reads two kinds of codes.

## 1. Plain repository URL

The simplest code just contains the URL of your repository or releases page:

```text
https://github.com/owner/harbour-myapp
```

This works for any repository whose type Harpoon can tell from the host: GitHub, Codeberg,
GitLab.com, SourceHut, SourceForge and so on.

## 2. A `harpoon://add` link

Use a link when Harpoon cannot guess the source from the URL. For example:

- a self-hosted Forgejo, Gitea or GitLab;
- a Jenkins job;
- an rpm-md repository, which also needs the package name.

```text
harpoon://add?url=<percent-encoded URL>[&source=<type>][&package=<RPM name>]
```

| Parameter | Meaning |
|---|---|
| `url` | Required. An `http(s)` address, percent-encoded. |
| `source` | Optional. The source type: `GitHub`, `Forgejo`, `GitLab`, `SourceHut`, `SourceForge`, `Jenkins`, `HTML`, `DirectLink` or `RpmMdRepo`. |
| `package` | Optional. The RPM package name. Required for `RpmMdRepo`. |

Harpoon shows the add dialog filled in from the link, and the user still confirms. Links
cannot change any other setting and never install anything by themselves.

The same link also works when it is opened on the phone, for example tapped on a web page or
read by the camera app. Harpoon registers itself as the handler for `harpoon:` links.

### Making the link

`harpoon-cli link` builds a link and checks that Harpoon can use it:

```sh
harpoon-cli link https://git.example.org/me/harbour-myapp --source Forgejo
# harpoon://add?url=https%3A%2F%2Fgit.example.org%2Fme%2Fharbour-myapp&source=Forgejo

harpoon-cli link https://repo.example.org/obs/sailfishos_5.0_aarch64 --source RpmMdRepo --package harbour-myapp
```

You can also write the link by hand: percent-encode the URL, at least `:`, `/`, `?`, `&` and
`=`.

## Making the QR code

Any QR generator works. With `qrencode`:

```sh
qrencode -o add-to-harpoon.png -s 8 -m 2 "https://github.com/owner/harbour-myapp"
qrencode -o add-to-harpoon.png -s 8 -m 2 "$(harpoon-cli link https://git.example.org/me/harbour-myapp --source Forgejo)"
```

Tips:

- Put the code in your README next to the download instructions, for example:
  `![Add to Harpoon](add-to-harpoon.png)`.
- Keep a quiet zone (white margin) around the code, and make it at least about 3 cm wide
  on screen.
- Shorter text gives a simpler code that is easier to scan: prefer the plain URL when it is
  enough.
- Users can also save a screenshot of the code and choose **Read from image** in the scanner.
