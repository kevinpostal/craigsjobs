from django.db import models
from django.contrib.sites.models import Site

class post(models.Model):
    
    sites = models.ForeignKey(Site)
    page_url =  models.URLField(max_length=420,unique=True)
    title = models.CharField(max_length=420,)
    summary = models.TextField()
    creation_date = models.DateField(auto_now_add=True)
    active = models.BooleanField()
    email = models.EmailField()

    def __unicode__(self):
        return self.title
