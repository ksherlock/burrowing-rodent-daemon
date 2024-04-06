package main
import ("os";"os/exec";"fmt";"log";"sort";"strings")


const kGopherDir byte = '1'
const kGopherText byte = '0'
const kGopherBin byte = '9'


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



func gopher_type(path string) byte {
	fi, err := os.Stat(path)
	if err != nil { return kGopherBin }
	if fi.IsDir() { return kGopherDir }
	if !fi.Mode().IsRegular() { return 0 }

	if fi.Size() == 0 { return kGopherText }


	f, err := os.Open(path)

	if err != nil { return kGopherBin }


	buffer := make([]byte, 4096)
	n, err := f.Read(buffer)
	f.Close()

	if err != nil { return kGopherBin }

	// cr := 0
	lf := 0
	for i := 0; i < n; i++ {
		b := buffer[i]

		/* assume ascii? */
		// if b > 0x7f { return kGopherBin }
		switch b {
		case 0x00: return kGopherBin
		case 0x0a: lf++
		case 0x0d: return kGopherBin
		}

	}
	if lf == 0 { return kGopherBin }

	return kGopherText;


}

func build_gopher_map(path string) {

	f, err := os.Open(path)
	if err != nil {
		log.Fatal("open ", path, ": ", err)
	}

	names, err := f.Readdirnames(-1)
	f.Close()

	if err != nil {
		log.Print("readdirnames ", path,": ", err)
		return
	}

	sort.Slice(names, func(i, j int) bool {
		return names[i] < names[j]
	})

	f, err = os.Create(path + "/gophermap", )
	if err != nil {
		log.Fatal("open ", path + "/gophermap", ": ", err)
	}


	dirs := make([]string, 0, 10)

	maxlen := 16
	for _, name := range(names) {
		l := len(name)
		if l > maxlen && l < 40 { maxlen = l }
	}

	// if maxlen > 20 { maxlen = 20 }

	for _, name := range(names) {

		if exclude[name] != 0 {
			continue
		}

		/* should use .gitattributes to check if a file is binary */
		t := gopher_type(path + "/" + name)
		if t == kGopherDir {
			dirs = append(dirs, name)
		}

		cmd := exec.Command("git", "log", "-1", "--pretty=format:%s", name)
		cmd.Dir = path
		comment := ""
		bb, err := cmd.Output()
		if err == nil {
			comment = sanitize_string(string(bb))
		}

		f.WriteString(fmt.Sprintf("%c%-*s %s\t%s\n", t, maxlen, name, comment, name))
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

	exclude = map[string]int{"gophermap": 1, ".git": 1, ".": 1, "..": 1}


	cmd := exec.Command("git", "checkout", "-f")
	cmd.Dir = worktree
	err := cmd.Run()
	if err != nil {
		log.Fatal("git checkout -f: ", err)
	}

	build_gopher_map(worktree)

}
