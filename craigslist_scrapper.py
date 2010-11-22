 #!/usr/bin/python
import os, sys, re, urllib
import feedparser
import settings

from django.core.management import setup_environ
setup_environ(settings)

from craig_scrape.models import post
from django.contrib.sites.models import Site


site_obj = Site.objects.all()

x = 0

for item in site_obj:
    site = item.domain
    current_site = Site.objects.get(domain=site)

    channels = feedparser.parse(site)
    strip_html = re.compile(r'<.*?>')
    strip_unicode = re.compile(r'[\x90-\x97\xa0-\xa3]')
    email_pattern = re.compile("[-a-zA-Z0-9._]+@[-a-zA-Z0-9_]+.[a-zA-Z0-9_.]+")

    
    for entry in channels.entries:
        try:
	    a = strip_html.sub('', entry.summary)
        
	except:
		pass
        summary = strip_unicode.sub('', a)

        try:
            e = post.objects.get(page_url=entry.link)
            
        except:
        
            try:
                html = urllib.urlopen(entry.link).read()
        
                #Grab and Remove Duplicate Emails
                emails = re.findall(email_pattern, html)
                emails = list(set(emails))
                emails = emails.pop()
            
                try:
                    emails = strip_unicode.sub('', emails)
            
                except:  
                    pass
            
            except:
                pass
    
            post.objects.get_or_create(sites=current_site,page_url=entry.link,title=entry.title,summary=summary,email=emails)
            print "%s %s \n\n" % (strip_unicode.sub('', entry.title),emails)
        x = x + 1
        print x



