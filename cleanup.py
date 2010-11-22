 #!/usr/bin/python
import datetime
import settings
from django.core.management import setup_environ
setup_environ(settings)

from craig_scrape.models import post

fiveday = datetime.timedelta(days=5)
today = datetime.date.today()

start_date = datetime.date(2010, 1, 1)
end_date = today - fiveday 

site_obj = post.objects.filter(creation_date__range=(start_date, end_date))

print "%s Items Removed" % site_obj.count()

site_obj.delete()