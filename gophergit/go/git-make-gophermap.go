package main
import ("os";"os/exec";"fmt";"log";"sort";"strings")


var exclude map[string]int

func sanitize_string(s string) string {

	s = strings.TrimSpace(s)
	delta := false
	for _, c := range(s)  {
		if c < 0x20 || c > 0x7f {
			delta = true
			break
		}

	}
	if !delta {
		return s
	}

	var b strings.Builder
	b.Grow(len(s))
	for _, c := range(s)  {
		if c > 0x7f {
			continue
		}
		if c < 0x20 {
			c = 0x20
		}
		b.WriteRune(c)
	}

	s = b.String()
	if len(s) > 35 {
		return s[:32] + "..."
	}
	return s
}


func is_binary(path string) bool {
	/* git checks the first 8000 bytes for a nul char */
	/* since gopher text files are line-oriented, we check for \r as well. */

	/* todo -- `run git check-attr --all` to check .gitattributes? */

	f, err := os.Open(path)
	if err != nil { return true; }


	buffer := make([]byte, 4096)
	n, err := f.Read(buffer)
	f.Close()

	if err != nil { return true }

	// cr := 0
	lf := 0
	for i := 0; i < n; i++ {
		b := buffer[i]

		switch b {
		case 0x00: return true
		case 0x0a: lf++
		case 0x0d: return true
		}

	}
	if lf == 0 { return true }

	return false;

}

func build_gopher_map(path string) {

	f, err := os.Open(path)
	if err != nil {
		log.Fatal("open ", path, ": ", err)
	}

	info, err := f.Readdir(-1)
	f.Close()

	if err != nil {
		log.Print("readdir ", path,": ", err)
		return
	}


	sort.Slice(info, func(i, j int) bool {
		return info[i].Name() < info[j].Name()
	})

	f, err = os.Create(path + "/gophermap", )
	if err != nil {
		log.Fatal("open ", path + "/gophermap", ": ", err)
	}


	dirs := make([]string, 0, 10)

	for _, fi := range(info) {

		name := fi.Name()
		if exclude[name] != 0 {
			continue
		}

		/* should use .gitattributes to check if a file is binary */
		t := '0'
		if fi.IsDir() {
			dirs = append(dirs, name)
			t = '1'
		} else if is_binary(path + "/" + name) {
			t = '9'
		}

		cmd := exec.Command("git", "log", "-1", "--pretty=format:%s", name)
		cmd.Dir = path
		comment := ""
		bb, err := cmd.Output()
		if err == nil {
			comment = sanitize_string(string(bb))
		}

		f.WriteString(fmt.Sprintf("%c%-16s %s\t%s\n", t, name, comment, name))
	}
	f.Close()

	for _, dir := range(dirs) {

		build_gopher_map(path + "/" + dir)
	}
}

func main() {

	if len(os.Getenv("GIT_DIR")) == 0 {
		log.Fatal("$GIT_DIR not defined")
	}

	if (len(os.Args) != 2) {
		log.Fatal("Usage: git-gopher worktree");
	}
	worktree := os.Args[1]

	os.Setenv("GIT_WORK_TREE", worktree)

	exclude = map[string]int{"gophermap": 1, ".git", ".": 1, "..": 1}


	cmd := exec.Command("git", "checkout", "-f")
	cmd.Dir = worktree
	err := cmd.Run()
	if err != nil {
		log.Fatal("git checkout -f: ", err)
	}

	build_gopher_map(worktree)

}
