#include <assert.h>
#include <string.h>
#include "trending.h"

int main(void) {
    char html[] =
        "<article><a href=\"/stargazers\"></a></article>"
        "<article><a href=\"/bad//stargazers\"><svg></svg>1</a></article>"
        "<article><a href=\"/one/repo/stargazers\"><svg></svg> 13,456 </a>"
        "<a href=\"/one/repo/forks\">99</a><span>1 star today</span></article>"
        "<article><a href=\"/two/big/stargazers\"><svg></svg>42</a></article>"
        "<article><a href=\"/three/partial/stargazers\"><svg></svg>23</a>";
    trend_project projects[3];
    assert(trend_parse(html, projects, 3) == 2);
    assert(!strcmp(projects[0].owner, "one") && !strcmp(projects[0].repo, "repo"));
    assert(projects[0].stars == 13456 && projects[0].daily == 1);
    assert(projects[1].stars == 42 && projects[1].daily == -1);
    assert(trend_parse(html, projects, 1) == 1);
}
